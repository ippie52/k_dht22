//------------------------------------------------------------------------------
/// \file   dht22.c
/// \brief  Application to read and sanity check the output form the DHT21/22.
///
/// This is based on the loldht application provided by the repository found at
/// https://github.com/technion/lol_dht22, previously amended by
/// technion@lolware.net, and finally updated by Kris Dunning: ippie52@gmail.com
//------------------------------------------------------------------------------
//                            Kris Dunning 2016
//------------------------------------------------------------------------------

#include <wiringPi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>

#include "locking.h"

static const float MAX_HUMIDITY = 99.9f;
static const int MAX_LOCKING_PATH_LENGTH = 100;
static const int DEFAULT_PIN = 7;
static const int MAX_TIMINGS = 85;

// -----------------------------------------------------------------------------
// The result enumeration of the sensor readings
// -----------------------------------------------------------------------------
typedef enum Results
{
    RESULT_OK,        /* Valid values appear to have been found */
    RESULT_BAD_DATA,  /* Bad data                               */
    RESULT_ALL_ZERO,  /* All values are zero - suspicious       */
    RESULT_INVALID,   /* Data appears to be invalid             */
} SensorReadingResults;

// -----------------------------------------------------------------------------
// Sensor value struct, storing temperature, humidity and the processing result
// -----------------------------------------------------------------------------
typedef struct Values
{
    SensorReadingResults result;
    float humidity;
    float temperature;

} SensorValues;

/*******************************************************************************
 *  \brief  Evaluates the sensor values to sanity check the results found.
 *  \return SensorReadingResults value to indicate the legitimacy of the results
 *          obtained.
 */
static int evaluate
(
    const SensorValues * const values ///<IN - The SensorValues to evaluate
)
{
    if (MAX_HUMIDITY < values->humidity)
    {
      fprintf(stderr, "Error: Humidity out of range\n");
      return RESULT_INVALID;
    }

    if (0.0f == values->humidity && 0.0f == values->temperature)
    {
        fprintf(stderr, "Warning: Humidity and temperature both zero (suspicious)\n");
        return RESULT_ALL_ZERO;
    }

    return RESULT_OK;
}

/*******************************************************************************
 *  \brief  This evaluates and sanitises the value read. It should be within
 *          range of a single byte.
 *  \return The sanitised read value
 */
static uint8_t sizecvt
(
    const int read ///<IN - The value to sanitise
)
{
    /* digitalRead() and friends from wiringPi are defined as returning a value
    < 256. However, they are returned as int() types. This is a safety function */

    if (read > 255 || read < 0)
    {
        printf("Invalid data from wiringPi library\n");
        exit(EXIT_FAILURE);
    }
    return (uint8_t)read;
}

/*******************************************************************************
 *  \brief  Reads the DHT22 value and returns the result of the read.
 *  \return The SensorReadingResults value.
 */
static SensorReadingResults read_dht22_data
(
    const int sensor_pin,   ///<IN - The sensor pin to read
    SensorValues *values    ///<OUT - The values to set
)
{
    uint8_t laststate = HIGH;
    uint8_t counter = 0;
    uint8_t j = 0, i;
    int data_sum = 0;
    static int dht22_data[5] = { 0, 0, 0, 0, 0 };

    memset(dht22_data, 0, sizeof(dht22_data));

    // Pull pin down for 18 milliseconds
    pinMode(sensor_pin, OUTPUT);
    digitalWrite(sensor_pin, HIGH);
    delay(10);
    digitalWrite(sensor_pin, LOW);
    delay(18);
    // Then pull it up for 40 microseconds
    digitalWrite(sensor_pin, HIGH);
    delayMicroseconds(40);
    // Prepare to read the pin
    pinMode(sensor_pin, INPUT);

    // Detect change and read data
    for (i = 0; i < MAX_TIMINGS; ++i)
    {
        counter = 0;
        while (sizecvt(digitalRead(sensor_pin)) == laststate)
        {
            ++counter;
            delayMicroseconds(1);
            if (0xFF == counter)
            {
                break;
            }
        }
        laststate = sizecvt(digitalRead(sensor_pin));

        if (0xFF == counter)
        {
            break;
        }

        // Ignore the first 3 transitions
        if ((i >= 4) && ((i % 2) == 0))
        {
            // Shove each bit into the storage bytes
            dht22_data[j/8] <<= 1;
            if (counter > 16)
            {
                dht22_data[j/8] |= 1;
            }
            j++;
        }
    }

    // Check we read 40 bits (8bit x 5 ) + verify checksum in the last byte
    data_sum = (dht22_data[0] + dht22_data[1] + dht22_data[2] + dht22_data[3]);
    if ((j >= 40) &&
    (dht22_data[4] == (uint8_t)(data_sum & 0xFF)))
    {
        values->humidity = (float)dht22_data[0] * 256 + (float)dht22_data[1];
        values->humidity /= 10;
        values->temperature = (float)(dht22_data[2] & 0x7F)* 256 + (float)dht22_data[3];
        values->temperature /= 10.0;

        if ((dht22_data[2] & 0x80) != 0)
        {
          values->temperature *= -1.0;
        }
        values->result = evaluate(values);
    }
    else
    {
        fprintf(stderr, "Data not good, skip\n");
        values->result = RESULT_BAD_DATA;
    }
    return values->result;
}

/*******************************************************************************
 *  \brief  Main function.
 *  \return Result of the sensor evaluation.
 */
int main
(
    int argc,       ///<IN - The number of arguments
    char *argv[]    ///<IN - The collection of argument strings
)
{
    int lockfd;
    int dht_pin = DEFAULT_PIN;
    int zero_count = 0;
    int tries = 100;
    char buffer[MAX_LOCKING_PATH_LENGTH];

    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <pin> (<tries>)\n", argv[0]);
        fprintf(stderr, "Description:\n\tPin is the wiringPi pin number (default 7 (GPIO 4)).\n");
        fprintf(stderr, "\tTries is the number of times to try to obtain a read (default 100) [Optional]");
    }
    else
    {
        dht_pin = atoi(argv[1]);
        fprintf(stderr, "Setting sensor pin to %d\n", dht_pin);
    }

    if (argc == 3)
    {
        tries = atoi(argv[2]);
    }
    fprintf(stderr, "%d attempts will be made.\n", tries);

    if (tries < 1)
    {
        fprintf(stderr, "Invalid tries supplied\n");
        exit(EXIT_FAILURE);
    }

    get_lockfile_name(dht_pin, buffer, MAX_LOCKING_PATH_LENGTH);
    lockfd = open_lockfile(buffer);

    if (wiringPiSetup() == -1)
    {
        fprintf(stderr, "Problem setting up wiringPi\n");
        exit(EXIT_FAILURE);
    }

    if (setuid(getuid()) < 0)
    {
        perror("Dropping privileges failed\n");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "Beginning read attempts\n");
    SensorValues values = { 0 };
    while (tries--)
    {
        fprintf(stderr, "Reading values...\n");
        if (read_dht22_data(dht_pin, &values) == RESULT_ALL_ZERO)
        {
            fprintf(stderr, "Reading was zero, checking again\n");
            ++zero_count;
            if (2 <= zero_count)
            {
                values.result = RESULT_OK;
                break;
            }
            ++tries;
        }

        if (RESULT_OK == values.result)
        {
            break;
        }

        if (RESULT_OK != values.result)
        {
            delay(1000); // wait 1sec to refresh
        }
    }

    if (RESULT_OK == values.result)
    {
        printf("Humidity = %.2f %% Temperature = %.2f *C \n", values.humidity, values.temperature);
    }
    else
    {
        fprintf(stderr, "Values could not be obtained.\n");
    }

    delay(1500);
    close_lockfile(lockfd);

    return 0;
}
