/*
 * This is taken from the bfd info pages.  The only change is the target
 * has been changed to elf32-i386 as that is one of the targets
 * supported on my system's bfd install.  All else is the same.  Works
 * quite fine.
 */

#include "bfd.h"

main()
{
     bfd *abfd;
     asymbol *ptrs[2];
     asymbol *new;
     
     abfd = bfd_openw("foo","elf32-i386");
     bfd_set_format(abfd, bfd_object);
     new = bfd_make_empty_symbol(abfd);
     new->name = "dummy_symbol";
     new->section = bfd_make_section_old_way(abfd, ".text");
     new->flags = BSF_GLOBAL;
     new->value = 0x12345;
     
     ptrs[0] = new;
     ptrs[1] = (asymbol *)0;
     
     bfd_set_symtab(abfd, ptrs, 1);
     bfd_close(abfd);
}
