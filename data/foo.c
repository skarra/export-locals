#include <bfd.h>

extern int
brew_get_local_sym_count (bfd *b, asymbol **sym_tab);

int main (void)
{
    foo();
    return (brew_get_local_sym_count(0, 0));
}
