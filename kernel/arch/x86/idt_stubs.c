/**
 * Copyright 2019, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * X86 IDT Assembler stubs
 */

#include <arch/x86/idt_stubs.h>

idt_stub_t IdtStubs[IDT_DESCRIPTOR_COUNT] = {
    irq_handler0, irq_handler1, irq_handler2, irq_handler3, irq_handler4, 
    irq_handler5, irq_handler6, irq_handler7, irq_handler8, irq_handler9, 
    irq_handler10, irq_handler11, irq_handler12, irq_handler13, 
    irq_handler14, irq_handler15, irq_handler16, irq_handler17, 
    irq_handler18, irq_handler19, irq_handler20, irq_handler21, 
    irq_handler22, irq_handler23, irq_handler24, irq_handler25, 
    irq_handler26, irq_handler27, irq_handler28, irq_handler29, 
    irq_handler30, irq_handler31, irq_handler32, irq_handler33, 
    irq_handler34, irq_handler35, irq_handler36, irq_handler37, 
    irq_handler38, irq_handler39, irq_handler40, irq_handler41, 
    irq_handler42, irq_handler43, irq_handler44, irq_handler45, 
    irq_handler46, irq_handler47, irq_handler48, irq_handler49, 
    irq_handler50, irq_handler51, irq_handler52, irq_handler53, 
    irq_handler54, irq_handler55, irq_handler56, irq_handler57, 
    irq_handler58, irq_handler59, irq_handler60, irq_handler61, 
    irq_handler62, irq_handler63, irq_handler64, irq_handler65, 
    irq_handler66, irq_handler67, irq_handler68, irq_handler69, 
    irq_handler70, irq_handler71, irq_handler72, irq_handler73, 
    irq_handler74, irq_handler75, irq_handler76, irq_handler77, 
    irq_handler78, irq_handler79, irq_handler80, irq_handler81, 
    irq_handler82, irq_handler83, irq_handler84, irq_handler85, 
    irq_handler86, irq_handler87, irq_handler88, irq_handler89, 
    irq_handler90, irq_handler91, irq_handler92, irq_handler93, 
    irq_handler94, irq_handler95, irq_handler96, irq_handler97, 
    irq_handler98, irq_handler99, irq_handler100, irq_handler101, 
    irq_handler102, irq_handler103, irq_handler104, irq_handler105, 
    irq_handler106, irq_handler107, irq_handler108, irq_handler109, 
    irq_handler110, irq_handler111, irq_handler112, irq_handler113, 
    irq_handler114, irq_handler115, irq_handler116, irq_handler117, 
    irq_handler118, irq_handler119, irq_handler120, irq_handler121, 
    irq_handler122, irq_handler123, irq_handler124, irq_handler125, 
    irq_handler126, irq_handler127, irq_handler128, irq_handler129, 
    irq_handler130, irq_handler131, irq_handler132, irq_handler133, 
    irq_handler134, irq_handler135, irq_handler136, irq_handler137, 
    irq_handler138, irq_handler139, irq_handler140, irq_handler141, 
    irq_handler142, irq_handler143, irq_handler144, irq_handler145, 
    irq_handler146, irq_handler147, irq_handler148, irq_handler149, 
    irq_handler150, irq_handler151, irq_handler152, irq_handler153, 
    irq_handler154, irq_handler155, irq_handler156, irq_handler157, 
    irq_handler158, irq_handler159, irq_handler160, irq_handler161, 
    irq_handler162, irq_handler163, irq_handler164, irq_handler165, 
    irq_handler166, irq_handler167, irq_handler168, irq_handler169, 
    irq_handler170, irq_handler171, irq_handler172, irq_handler173, 
    irq_handler174, irq_handler175, irq_handler176, irq_handler177, 
    irq_handler178, irq_handler179, irq_handler180, irq_handler181, 
    irq_handler182, irq_handler183, irq_handler184, irq_handler185, 
    irq_handler186, irq_handler187, irq_handler188, irq_handler189, 
    irq_handler190, irq_handler191, irq_handler192, irq_handler193, 
    irq_handler194, irq_handler195, irq_handler196, irq_handler197, 
    irq_handler198, irq_handler199, irq_handler200, irq_handler201, 
    irq_handler202, irq_handler203, irq_handler204, irq_handler205, 
    irq_handler206, irq_handler207, irq_handler208, irq_handler209, 
    irq_handler210, irq_handler211, irq_handler212, irq_handler213, 
    irq_handler214, irq_handler215, irq_handler216, irq_handler217, 
    irq_handler218, irq_handler219, irq_handler220, irq_handler221, 
    irq_handler222, irq_handler223, irq_handler224, irq_handler225, 
    irq_handler226, irq_handler227, irq_handler228, irq_handler229, 
    irq_handler230, irq_handler231, irq_handler232, irq_handler233, 
    irq_handler234, irq_handler235, irq_handler236, irq_handler237, 
    irq_handler238, irq_handler239, irq_handler240, irq_handler241, 
    irq_handler242, irq_handler243, irq_handler244, irq_handler245, 
    irq_handler246, irq_handler247, irq_handler248, irq_handler249, 
    irq_handler250, irq_handler251, irq_handler252, irq_handler253, 
    irq_handler254, irq_handler255
};
