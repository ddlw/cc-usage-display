# Third-party notices

This project is derived from
[peterroe/magpie-usage-display](https://github.com/peterroe/magpie-usage-display),
which is Copyright (c) 2026 Jay Yao and licensed under the MIT License. The
original copyright and license text are retained in LICENSE.

The UI uses fonts bundled with LVGL. Montserrat is Copyright 2024 The
Montserrat.Git Project Authors and is licensed under the SIL Open Font License
1.1. See LICENSES/OFL-1.1.txt.

The project downloads but does not vendor the following build/runtime
dependencies:

| Dependency | License |
|---|---|
| ArduinoJson | MIT |
| LVGL | MIT |
| Arduino GFX Library for Arduino | BSD 2-Clause |
| XPowersLib | MIT |
| Arduino-ESP32 | LGPL-2.1 |
| pioarduino platform-espressif32 | Apache-2.0 |
| PlatformIO Core | Apache-2.0 |

The Windows companion uses only the Python standard library. CC Switch is an
independent application and is not bundled with this project; its database is
opened read-only as a local data source.

Waveshare board examples and documentation were used as hardware references,
but no Waveshare example source is vendored in this repository.

Each dependency remains subject to its own copyright notices and license
terms. Refer to platformio.ini for the pinned firmware dependency versions.
