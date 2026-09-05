add_module_target(xhci ""
    controller.c
    interrupt.c
    port.c
    transfer.c
)
target_link_libraries(xhci PRIVATE usb-common)
deploy_file_to_initrd(xhci "modules" xhci.yaml)
