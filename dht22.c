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
#include <sched.h>

#include "locking.h"

static const float MAX_HUMIDITY = 99.9f;
static const int MAX_PATH_LENGTH = 100;
static const int MAX_READING_LENGTH = 20;
static const int DEFAULT_PIN = 7;
static const int MAX_TIMINGS = 85;

// -----------------------------------------------------------------------------
// The result enumeration of the sensor readings
// -----------------------------------------------------------------------------
typedef enum Results
{
    RESULT_OK,          /* Valid values appear to have been found   */
    RESULT_BAD_DATA,    /* Bad data                                 */
    RESULT_ALL_ZERO,    /* All values are zero - suspicious         */
    RESULT_INCONSISTENT,/* Data inconsistent from last reading      */
    RESULT_INVALID,     /* Data appears to be invalid               */
} SensorReadingResults;

/*******************************************************************************
 *  \brief  Sensor value struct, storing temperature, humidity and the
 *          processing result
 */
typedef struct Values
{
    SensorReadingResults result;    ///< The sensor reading results
    float humidity;                 ///< The humidity reading (in %)
    float temperature;              ///< The temperature reading (in *C)

} SensorValues;

#define INVALID_VALUES  { RESULT_INVALID, 0.0f, 0.0f }
#define C_TO_F(c)       (((float)c * 1.8f) + 32.0f)

/*******************************************************************************
 *  \brief  Evaluates the sensor values to sanity check the results found.
 *  \return SensorReadingResults value to indicate the legitimacy of the results
 *          obtained.
 */
static int evaluate
(
    const SensorValues * const values   ///<IN - The SensorValues to evaluate
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
 *  \brief  Evaluates the sensor values to sanity check the results found
 *          against previous read and stored values.
 *  \return SensorReadingResults value to indicate the legitimacy of the results
 *          obtained.
 */
static int evaluate_last
(
    const SensorValues last_stored, ///<IN - The last SensorValues stored on file
    SensorValues *values,           ///<IN/OUT - The SensorValues to evaluate
    SensorValues *last_read         ///<OUT - The last read values for comparison
)
{
    values->result = evaluate(values);
    if (RESULT_OK == values->result && RESULT_OK == last_stored.result)
    {
        // First, let's check whether its similar enough
        if (abs(last_stored.temperature - values->temperature) > 5.0f ||
            abs(last_stored.humidity - values->humidity) > 5.0f)
        {
            // Now, let's check to see whether we have a previous reading, and if so, whether
            // the temperature or humidity has genuinely changed this much
            if (RESULT_INCONSISTENT == last_read->result &&
                abs(last_read->temperature - values->temperature) < 5.0f &&
                abs(last_read->humidity - values->humidity) < 5.0f)
            {
                fprintf(stderr, "Last two read values appear to match, ignoring saved inconsistency");
                // We can assume the value(s) have actually changed this much
                values->result = RESULT_OK;
            }
            else
            {
                fprintf(stderr, "Last value seems inconsistent, reading again\n");
                // Either the value doesn't match up, keep trying, or this is the first check
                values->result = RESULT_INCONSISTENT;
            }
        }
        *last_read = *values;
    }

    return values->result;
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
 *  \brief  This returns the file pointer to a stored sensor file
 *  \return The file pointer to the sensor reading file, NULL if fail.
 */
static FILE *get_sensor_file_descriptor
(
    const int sensor_pin,   ///<IN - The sensor pin ID used to identify the file
    const char *open_mode   ///<IN - The file open mode, i.e. "r+"
)
{
    char filename[MAX_PATH_LENGTH];
    FILE *fp = NULL;
    snprintf(filename, MAX_PATH_LENGTH, "/tmp/dhtsensor.%d", sensor_pin);
    fp = fopen(filename, open_mode);
    if (NULL == fp)
    {
        fprintf(stderr, "Failed to open file %s in mode \"%s\".\n",
            filename, open_mode);
    }
    return fp;
}

/*******************************************************************************
 *  \brief  Gets the last sensor values, if stored.
 *  \return Returns the SensorValues interpreted from the saved values.
 */
static SensorValues get_last_values
(
    const int sensor_pin    ///<IN - The sensor pin to check
)
{
    SensorValues values = INVALID_VALUES;
    char contents[MAX_READING_LENGTH];
    size_t size = 0L;
    FILE *fp = get_sensor_file_descriptor(sensor_pin, "r");
    if (fp)
    {
        fseek(fp, 0L, SEEK_END);
        size = ftell(fp);
        rewind(fp);
        int read_pin;
        int read_temp;
        int read_hum;
        if (size < MAX_READING_LENGTH)
        {
            if (fread(contents, size, 1, fp) <= size)
            {
                sscanf(contents, "%d %d %d", &read_pin, &read_temp, &read_hum);
                if (read_pin != sensor_pin)
                {
                    fprintf(stderr, "Read pin does not match expected: %d != %d\n",
                        read_pin, sensor_pin);
                }
                else
                {
                    values.temperature = (float)(read_temp) / 1000.0f;
                    values.humidity = (float)(read_hum) / 1000.0f;
                    values.result = evaluate(&values);
                }
            }
            else
            {
                fprintf(stderr, "Problem reading contents of sensor file\n");
            }
        }
        fclose(fp);
    }
    return values;
}

/*******************************************************************************
 *  \brief  Stores the last sensor values.
 *  \return Zero if storing fails, otherwise 1.
 */
static int set_last_values
(
    const int sensor_pin,       ///<IN - The sensor pin to store
    const SensorValues values   ///<IN - The sensor value readings to store
)
{
    int result = 0;
    FILE *fp = get_sensor_file_descriptor(sensor_pin, "w");
    if (fp)
    {
        fprintf(fp, "%d %06d %06d",
            sensor_pin,
            (int)(1000 * (values.temperature + 0.5f)),
            (int)(1000 * (values.humidity + 0.5f)));
        fclose(fp);
        result = 1;
    }
    else
    {
        fprintf(stderr, "Error: Could not write to file.\n");
    }
    return result;
}

/*******************************************************************************
 *  \brief  Sets the thread priority to the maximum available in the hope that
 *          it will prevent data loss when bit-bashing the DHT sensor.
 */
static void set_priority()
{
    struct sched_param params = { sched_get_priority_max(SCHED_FIFO) };
    // PID set to zero implies this thread, FIFO is the best chance at having a
    // "real-time" priority, and the maximum priority is identified.
    sched_setscheduler(0, SCHED_FIFO, &params);
}

/*******************************************************************************
 *  \brief  Reads the DHT22 value and returns the result of the read.
 *  \return The SensorReadingResults value.
 */
static SensorReadingResults read_dht22_data
(
    const int sensor_pin,           ///<IN - The sensor pin to read
    SensorValues *values,           ///<OUT - The values to set
    const SensorValues last_stored  ///<IN - The last stored values
)
{
    uint8_t laststate = HIGH;
    uint8_t counter = 0;
    uint8_t j = 0, i;
    int data_sum = 0;
    static int dht22_data[5] = { 0, 0, 0, 0, 0 };
    static SensorValues last_read = INVALID_VALUES;

    memset(dht22_data, 0, sizeof(dht22_data));
    // Pull pin down for 18 milliseconds
    pinMode(sensor_pin, OUTPUT);
    digitalWrite(sensor_pin, HIGH);
    delayMicroseconds(10000);
    digitalWrite(sensor_pin, LOW);
    delayMicroseconds(18000);
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
    if ((j >= 40) && (dht22_data[4] == (uint8_t)(data_sum & 0xFF)))
    {
        values->humidity = (float)dht22_data[0] * 256 + (float)dht22_data[1];
        values->humidity /= 10;
        values->temperature = (float)(dht22_data[2] & 0x7F)* 256 + (float)dht22_data[3];
        values->temperature /= 10.0;
        if ((dht22_data[2] & 0x80) != 0)
        {
          values->temperature *= -1.0;
        }
        values->result = evaluate_last(last_stored, values, &last_read);
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
    char buffer[MAX_PATH_LENGTH];

    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <pin> (<tries>)\n", argv[0]);
        fprintf(stderr, "Description:\n\tPin is the wiringPi pin number (default 7 (GPIO 4)).\n");
        fprintf(stderr, "\tTries is the number of times to try to obtain a read (default %d) [Optional]", tries);
    }
    else
    {
        dht_pin = atoi(argv[1]);
        printf("Setting sensor pin to %d\n", dht_pin);
    }

    if (argc >= 3)
    {
        tries = atoi(argv[2]);
    }
    printf("%d attempts will be made.\n", tries);

    if (tries < 1)
    {
        fprintf(stderr, "Invalid tries supplied\n");
        exit(EXIT_FAILURE);
    }

    get_lockfile_name(dht_pin, buffer, MAX_PATH_LENGTH);
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

    SensorValues last_stored = get_last_values(dht_pin);
    if (RESULT_OK != last_stored.result)
    {
        fprintf(stderr, "Stored results were not OK, ignoring them.\n");
    }

    SensorValues values = INVALID_VALUES;
    // Set the thread priority to give a better chance of not losing data due to
    // thread interruptions
    set_priority();
    while (tries--)
    {
        if (read_dht22_data(dht_pin, &values, last_stored) == RESULT_ALL_ZERO)
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
            delay(200); // wait to refresh
        }
    }

    if (RESULT_OK == values.result)
    {
        printf("Humidity = %.2f %% Temperature = %.2f *C (%.2f *F)\n",
            values.humidity, values.temperature, C_TO_F(values.temperature));
    }
    else
    {
        fprintf(stderr, "Values could not be obtained.\n");
    }

    set_last_values(dht_pin, values);

    delay(100);
    close_lockfile(lockfd);

    return 0;
}
