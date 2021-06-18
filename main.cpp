/* mbed Microcontroller Library
 * Copyright (c) 2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"
#include "InputResult.h"
#include "TCPSocket.h"
#include "EthernetInterface.h"


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

            char testBuffer[bufferSize] = {0};
            memcpy(testBuffer, mailBuffer, bufferSize);
            auto obj = reinterpret_cast<InputResult *>(testBuffer);

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

int main()
{
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
}