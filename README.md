## Open ESP-IDF terminal on vsCode

1. Use shortcut `Ctrl+Shift+P` to open the `Command Pallet`. Next type `terminal`, on the suggested options, select `ESP-IDF: Open ESP-IDF Terminal`.
2. Or use the shortcut `Ctrl+E`  `T` (wait for split second after `Ctrl+E` before typing `T`)

## Build the app

1. Build the project: `idf.py build` or `idf.py app build`
2. Flash the project: `idf.py flash -p COMx` if a COM port already selected then use `idf.py flash`
3. Open the monitor: `idf.py monitor`, or flash and monitor `idf.py flash monitor`
   
Example work flow: <br>
```
idf.py app build
idf.py flash monitor -p COM4
```

## Configure variables

1. add variable to Kconfig.projbuild under `/main`
2. set variable value for the target chip e.g `sdkconfig.defaults.esp32` prefix with `CONFIG`
3. Clean, recofig, and build

```
idf.py fullclean
idf.py reconfigure
idf.py build
```