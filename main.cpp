/* mbed Microcontroller Library
 * Copyright (c) 2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"
#include "InputResult.h"
#include "TCPSocket.h"
#include "EthernetInterface.h"
#include <cstring>
#include <string>

// Blinking rate in milliseconds
#define SEND_RATE               2000ms
#define RECIVE_RATE             2000ms
#define RECIVE_UPDATE_RATE      200ms
#define MAIN_LOOP_RATE          300ms


// ======================================
// Ports initialization


BufferedSerial RsConnector(PD_5, PD_6, 9600);
const auto bufferSize = sizeof(InputResult);


// =====================================
// Leds Logic

DigitalOut MainLed(LED1);
DigitalOut ReciveLed(LED2);
DigitalOut SendLed(LED3);

volatile bool IsMainLedBlinking = false;
volatile bool IsSendLedBlinking = false;
volatile bool IsReciveLedBlinking = false;

void ChangeBlinkState(volatile bool& conidion, DigitalOut &port) 
{
    if (conidion) 
    {
        port = 1;
        conidion = false;
    } 
    else
        port = 0;
}

// ======================================
// Ethernet Logic

#define HOST_IP			"192.168.50.34"
#define HOST_PORT		5050
#define PORT            80


EthernetInterface*  net;
TCPSocket ReciveSocket;
TCPSocket SendSocket;
TCPSocket* client;


char httpBuf[1500];

SocketAddress HostAddress(HOST_IP, HOST_PORT);


int8_t ParseUrl(char *url) 
{
    if (strncmp(httpBuf, "POST", 4) != 0)
        return -1;

    char *cmd = ((url + 4 + 1 + 1)); // Skip command and /
    if (strcmp(cmd, "/setup?gate=0") == 0)
        return (0);

    if (strcmp(cmd, "/setup?gate=1") == 0)
        return (1);
    return (-1);
}


std::string MakeDataRequest(SensorData data) 
{
    auto content =
        std::string("{ \"AirTemperature\": ") +
        std::to_string(data.AirTemperature) +
        ", \"AirHumidity\": " + std::to_string(data.AirHumidity) +
        ", \"WaterHumidity\": " + std::to_string(data.WaterHumidity) +
        ",  \"AirDewpoint\": " + std::to_string(data.AirDewpoint) +
        ",  \"AirDewpointFast\": " + std::to_string(data.AirDewpointFast) + " }";

    auto str = std::string("POST /api/info/receive HTTP/1.1\r\n") +
                "Content-Length: " + std::to_string(content.size()) +
                "\r\nContent-Type: application/json\r\n\r\n" + content;

    return str;
}

void SendDataToServer(SensorData *data) 
{
    auto request = MakeDataRequest(*data);
    auto sendBuffer = request.c_str();
    SendSocket.send(sendBuffer, request.size());
}


// ======================================
// Main threads

Mail<InputResult, 4> mailbox;

void SendInfoToEthernetThread() 
{
    while (true)
    {
        auto mail = mailbox.try_get();

        if(mail)
        {
            IsSendLedBlinking = true;

            printf("\nRecive result - %d\n", mail->CodeResult);
            
            if(mail->CodeResult == 0)
            {
                auto data = mail->Data;
                printf("Air: Temperature - %4.2f, Humidity - %4.2f", data.AirTemperature, data.AirHumidity);
                printf(", Dewpoint - %4.2f, Dewpoint fast - %4.2f\n", data.AirDewpoint, data.AirDewpointFast);
                printf("Ground: Humidity - %4.2f\n", data.WaterHumidity);

                SendDataToServer(&data);
            }

            mailbox.free(mail);
        }
        
        ThisThread::sleep_for(SEND_RATE);
    }
}


void ReciveDataFromRsThread() 
{
    while (true) 
    {
        if(!RsConnector.readable())
            continue;

        char mailBuffer[bufferSize] = {0};
        auto readResult = RsConnector.read(mailBuffer, bufferSize);

        if(readResult < 0)
            continue;

        if(readResult != 0)
        {
            IsReciveLedBlinking = true;

            char saveBuffer[bufferSize] = {0};
            memcpy(saveBuffer, mailBuffer, bufferSize);
            auto obj = reinterpret_cast<InputResult *>(saveBuffer);

            auto mail = mailbox.try_alloc();
            mail->CodeResult = obj->CodeResult;
            mail->Data = obj->Data;

            mailbox.put(mail);
        }

        ThisThread::sleep_for(RECIVE_RATE);
    }
}

// ======================================

Thread reciveTrd;
Thread sendTrd;

int main(void)
{
    net = new EthernetInterface();
    net->connect();
    
    // Open a TCP socket
    nsapi_error_t rt = SendSocket.open(net);
    if (rt != NSAPI_ERROR_OK) 
    {
        printf("Could not open TCP Socket (%d)\r\n", rt);
        return 1;
    }

    SendSocket.set_timeout(200);
    SendSocket.set_blocking(false);
    rt = SendSocket.connect(HostAddress);
    if (rt != NSAPI_ERROR_OK) {
        printf("Could not connect TCP socket (%d)\r\n", rt);
    }

    SocketAddress   addr;
    net->get_ip_address(&addr);
    printf("IP address: %s\n", addr.get_ip_address() ? addr.get_ip_address() : "None");
    net->get_netmask(&addr);
    printf("Netmask: %s\n", addr.get_ip_address() ? addr.get_ip_address() : "None");
    net->get_gateway(&addr);
    printf("Gateway: %s\n", addr.get_ip_address() ? addr.get_ip_address() : "None");

    reciveTrd.start(callback(ReciveDataFromRsThread));
    sendTrd.start(callback(SendInfoToEthernetThread));

    while (true) 
    {
        ChangeBlinkState(IsMainLedBlinking, MainLed);
        ChangeBlinkState(IsSendLedBlinking, SendLed);
        ChangeBlinkState(IsReciveLedBlinking, ReciveLed);

        IsMainLedBlinking = true;
        ThisThread::sleep_for(MAIN_LOOP_RATE);
    }

    net->disconnect();
}