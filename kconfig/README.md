# Kconfig How-to


## Configuration files

For Kconfig configuration files to be found and used correctly by the build system,
they should be located in the `vendor/*/kconfig/targets` directory.


## Creating a configuration for a new target

If you want to create a Kconfig file for a new target, the easiest way is to copy:  
`core/kconfig/targets/config_default` to `vendor/*/kconfig/targets/<TARGET>`,  
where `<TARGET>` is the name of your new target.

Then you can modify the Kconfig configuration via the `make menuconfig` command.


## Modifying an existing configuration

To modify the configuration used by a specific `TARGET`, run the following command:

    make menuconfig TARGET=<target>

You will be presented with a terminal interface, where you can modify the configuration options.

* To navigate through the menus, use the arrow keys.
* To save the modified configuration, press `S`.  
  (NOTE: Do not change the path, or the name of the file!)
* To quit, press `Q`.


## Example

To enable or disable specific managers, navigate to:  
`Common` -> `Target` -> `Manager customization`,
where you can select or deselect managers.


## Kconfiglib

The build system uses Kconfiglib, a Kconfig implementation in Python 2/3.

For a more complete description and usage of the menuconfig and Kconfiglib,
visit the Kconfiglib project's homepage: https://pypi.org/project/kconfiglib/
