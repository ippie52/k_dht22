# Repository information

Driver for DHT22/AM2302 Temperature and humidity sensors on Raspberry Pi.
Based on dht11.c: http://ubuntuone.com/6mT9cTREz90BUfvQD1AGNy (license unknown).
Forked from the repository https://github.com/technion/lol_dht22.git
Requires wiringPi library.

This version was created to make slight improvements which allow for a more
reliable and faster reading with fewer delays.

# Building
`./configure`
`sudo make install`

# Running
Due to file locking and other aspects, super user access is required.

`sudo kdht`

# Example output
`sudo ./kdht 28 10
Reading DHT21/22 sensor on GPIO 28
10 attempts will be made.
Humidity = 62.10 % Temperature = 23.80 *C (74.84 *F)`

# Licence
As with previous licence: Public domain. Do what you want. No warranties.
