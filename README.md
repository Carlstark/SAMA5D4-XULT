# SAMA5D4 XPLAINED
Sourcecode for Supporting Periphery Modules Produced by Embest

---------------------------
### How to Build The Source Code

    Please refer to the shell script make.sh under source code directory.


---------------------------
### How to Configure Different LCD Modules

LCD8000-43t:

	linux-at91-linux-3.10/arch/arm/boot/dts/at91-sama5d4_xplained_hdmi.dts:
	resolution = <272>;

LCD8000-70t:

	linux-at91-linux-3.10/arch/arm/boot/dts/at91-sama5d4_xplained_hdmi.dts:
	resolution = <480>;

Note: The project is base on https://github.com/linux4sam. If you encounter any problem when building or running, please leave message to me.
