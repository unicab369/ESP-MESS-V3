## Open ESP-IDF terminal on vsCode

1. Use shortcut `Ctrl+Shift+P` to open the `Command Pallet`. Next type `terminal`, on the suggested options, select `ESP-IDF: Open ESP-IDF Terminal`.
2. Or use the shortcut `Ctrl+E`  `T` (wait for split second after `Ctrl+E` before typing `T`)

## Configure variables

1. add variable to Kconfig.projbuild under `/main`
2. set variable value for the target chip e.g `sdkconfig.defaults.esp32` prefix with `CONFIG`
3. Clean, recofig, and build

```
idf.py fullclean
idf.py reconfigure
idf.py build
```

## Build the app

1. Build the project: `idf.py build` or `idf.py app build`
2. Flash the project: `idf.py flash -p COMx` if a COM port already selected then use `idf.py flash`
3. Open the monitor: `idf.py monitor`, or flash and monitor `idf.py flash monitor`
   
Example work flow: <br>
```
idf.py app build
idf.py flash monitor -p COM4
```

## Change Folder name length

SD card folder name length is limited to 8 chars
to extend this
`idf.py menuconfig` > `Component Config` > `FAT Filesystem Suport` > `Long fileame support` > (prefer stack)
in the same menu, also set the `Max long filename length` (prefer 16)

## VSCode Extensions
Better comments
Back & Forth
es6-string-html
Live Server
Serial Monitor
WSL
Power Mode




## terminal command
view flash size: `esptool.py --port COMX flash_id`
view chip info:  `esptool.py --port COM4 chip_id`
config flash size: `idf.py menuconfig` > Serial flasher config > Flash Size

## custom partition table
`idf.py menuconfig` > Partition Table > Select Custom partition table CSV
create `partitions.csv` in the main project folder
check partition table: `idf.py partition-table`
flash partition table: `idf.py partition-table-flash`

flash app: `idf.py flash app`
flash all: `idf.py flash monitor`


flash storage:
build first: `idf.py build`
then flash: `esptool.py write_flash 0x210000 build/storage.bin`
monitor: `idf.py monitor`
(Note: depending on partition address)

Example partitions:
# Name,   Type, SubType,  Offset,  Size, Flags
nvs,      data, nvs,      0x9000,  0x6000,
phy_init, data, phy,      0xf000,  0x1000,
storage,  data, littlefs, 0x1000,  1M,
factory,  app,  factory,        ,    ,

## enable LittleFS
add this to idf_component.yml: `joltwallet/littlefs: "~=1.20.3"`

## add LittleFS partition manually
add this to `CMakeLists.txt` under the `main` folder:
littlefs_create_partition_image(storage ../flash_data FLASH_IN_PROJECT)
create `flash_data` folder (or your custom folder name) under the main project folder

## Memory Analytic
idf.py size
idf.py size-components
