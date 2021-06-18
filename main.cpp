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

AnalogOut RelayConnector(PC_0);


// =====================================
// Leds Logic

DigitalOut RelayLed(LED1);
DigitalOut ReciveLed(LED2);
DigitalOut SendLed(LED3);

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
// Relay Controll logic

InterruptIn button1(BUTTON1);
volatile bool button1_pressed = false; // Used in the main loop
volatile bool button1_enabled = true; // Used for debouncing
Timeout button1_timeout; // Used for debouncing

// Enables button when bouncing is over
void button1_enabled_cb(void)
{
    button1_enabled = true;
}

// ISR handling button pressed event
void button1_onpressed_cb(void)
{
    if (button1_enabled) { // Disabled while the button is bouncing
        button1_enabled = false;
        button1_pressed = true; // To be read by the main loop
        button1_timeout.attach(callback(button1_enabled_cb), 0.3); // Debounce time 300 ms
    }
}


void ChangeRelayState()
{
  auto newValue = !RelayLed;
  RelayLed = newValue;
  RelayConnector = newValue;
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

void ListenServerConnections()
{
    SocketAddress addr;

    ReciveSocket.open(net);
    ReciveSocket.bind(PORT);
    ReciveSocket.listen();

    while (true) 
    {
        client = ReciveSocket.accept();
        if (client) 
        {
            client->getpeername(&addr);
            client->recv(httpBuf, 1500);

            int result = ParseUrl(httpBuf);
            if (result) 
            {
                auto message = "HTTP/1.1 200 OK";
                SendSocket.send(message, strlen(message));
            } 
            else 
            {
                auto badMessage = "HTTP/1.1 403 BadRequest";
                SendSocket.send(badMessage, strlen(badMessage));
            }

            client->close();
        }
    }
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
Thread listenTrd;

int main(void)
{
    printf("I'm Alive!");
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

    
    button1.fall(callback(button1_onpressed_cb));


    listenTrd.start(callback(ListenServerConnections));
    reciveTrd.start(callback(ReciveDataFromRsThread));
    sendTrd.start(callback(SendInfoToEthernetThread));

    while (true) 
    {
        ChangeBlinkState(IsSendLedBlinking, SendLed);
        ChangeBlinkState(IsReciveLedBlinking, ReciveLed);

        if (button1_pressed) 
        {
            button1_pressed = false;
            ChangeRelayState();
        }

        ThisThread::sleep_for(MAIN_LOOP_RATE);
    }

    net->disconnect();
}