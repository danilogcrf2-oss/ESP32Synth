ESP32 (can be S3 or classic (DOWD-Q6))
PCM5102A (opcional, you can change the output mode)
4x push buttons
OLED I2C Display (you can change the i2c adress)

wiring:

buttons:
left   = 13
right  = 12
shoot  = 14
shield = 27

DAC:
BCK = 4
LCK = 15
DIN = 2
(on the ESP32-DOIT-DEVKIT-V1, on a protobard you can just put the PCM5102A DAC module asside the esp32 with this pin configs)

Display:
SCK = 22
SDA = 21