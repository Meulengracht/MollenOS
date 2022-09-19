add_module_target(ehci ""
    structures/itd.c
    structures/qh.c
    structures/td.c
    controller.c
    interrupt.c
    port.c
    queue.c
    transfer.c
    transfer_generic.c
    transfer_isoc.c
)
target_link_libraries(ehci PRIVATE usb-common)
deploy_file_to_initrd(ehci "modules" ehci.yaml)
