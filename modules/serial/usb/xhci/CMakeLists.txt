add_module_target(xhci ""
    command.c
    controller.c
    dma.c
    dwc3.c
    endpoint.c
    interrupt.c
    port.c
    queue.c
    ring.c
    transfer.c
)
target_link_libraries(xhci PRIVATE usb-common)
deploy_file_to_initrd(xhci "modules" xhci.yaml)
