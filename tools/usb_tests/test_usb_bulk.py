import usb.core
import usb.util

VID = 0xCAFE
PID = 0x4001

dev = usb.core.find(idVendor=VID, idProduct=PID)

if dev is None:
    raise RuntimeError("USBusKit device not found")

dev.set_configuration()

try:
    message = b"Hello USBusKit"

    print("Sending:", message)

    written = dev.write(0x01, message, timeout=2000)
    print("Bytes written:", written)

    response = dev.read(0x81, 64, timeout=2000)

    print("Received:", bytes(response))

finally:
    usb.util.dispose_resources(dev)