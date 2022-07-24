# XboxHDkey
Tool for locking/unlocking original Xbox HDD, based on **hdparm** code.
Modified **smartctl** didn't work for me, so I wrote my own **hdtool** clone.

## Usage
`xboxhdkey [options] <device>`

### Options
- **-e *file***
  compute HDD password from EEPROM file
- **-k *key***
  compute HDD password from HDD key (in hex)
- **-l**
  lock HDD
- **-u**
  unlock HDD (default)
- **-d**
  disable HDD security
- **-h**
  help screen
