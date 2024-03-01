# SpotPassDumper11

Extracts the raw DISA save image for the `boss` (SpotPass) module.

# Usage

Launch the application. It should start dumping immediately, and the output file will be located in:

`sd:/spotpass_cache/partitionA.bin`

If you have partitionB in your save archive, which currently isn't known to exist, the application will dump your entire DISA save image in:

`sd:/spotpass_cache/00000000`

Please join our [Discord server](https://discord.gg/wxCEY8MHvh) if you get a notice saying that you have partitionB, so that we can further investigate its origins.
