# Eyeotine Protocol (EP)

## Header
- Version (2 bits, bitmask 0xc0) = 0
- Packet Type (2 bit, bitmask 0x30)/Subtype (4 bits, bitmask 0x0f)

## Packet Types

### Management (header=0x00)
#### Association (header=0x00)
- Flags (8 bit)
  - Req/Resp (1 bit, bitmask 0x01)
  - (unused) (7 bit)
#### Disassociation (header=0x01)
- (none)
#### Ping (header=0x02)
- (none)

### Control (header=0x10)
#### Restart (header=0x10)
- (none)
#### Sync (header=0x11)
- Flags (8 bit)
  - Req/Resp (1 bit, bitmask 0x01)
  - (unused) (7 bit)
#### Camera Settings (header=0x12)
- (variable)

### Data (header=0x20)
#### Image Data (header=0x20)
- Timestamp/Millisecs since Sync mod 1024 (10 bit, bitmask 0xffc0)
- Fragment Number (6 bit, bitmask 0x3f)
- (raw data) (0..2200 byte)


## Behavior Specification
![ESP state machine](img/esp_state_machine.png)

![Server state machine](img/server_state_machine.png)
