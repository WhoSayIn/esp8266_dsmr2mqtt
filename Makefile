PATH="~/.platformio/penv/bin/:~/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/MacGPG2/bin"

default: help

help:
	@echo "Usage:"
	@echo "     make [command]"
	@echo "Available commands:"
	@grep -v '^_' Makefile | grep '^[^#[:space:]].*:' | grep -v '=' | grep -v '^default' | sed 's/:\(.*\)//' | xargs -n 1 echo ' -'

build:
	PATH=${PATH} platformio run

clean:
	PATH=${PATH} platformio run --target clean

serial-monitor:
	PATH=${PATH} pio -f -c atom serialports monitor --port /dev/tty.usbserial-A603RY49

upload:
	PATH=${PATH} platformio run --target program --environment arduino-mega
