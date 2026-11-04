# Third-party components

## Satellite tuning attribution

The BS/110-degree CS changes are adapted from
[2m3k/TCLTV-Bondriver-3wave](https://github.com/2m3k/TCLTV-Bondriver-3wave),
commit `2c7bb42` (MIT), reviewed at `c5f121e`. Thanks to 2m3k for
the satellite demodulator parameters, database routing, tuning-space and catalog work.
The upstream MIT license and copyright notice are retained.
No precompiled fork binary is shipped: the native payload is built from the
reviewed sources in this repository. Local changes centralize serialized route
restoration, reject unsupported satellite IDs, fix an early-return allocation leak,
keep terrestrial timeouts, and use Android logcat rather than an unbounded log file.
