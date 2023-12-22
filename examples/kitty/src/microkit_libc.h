/* Setup for getting printf functionality working */
static int
libc_microkit_putc(char c, FILE *file)
{
    (void) file; /* Not used by us */
    microkit_dbg_putc(c);
    return c;
}

/*
 * We don't have a getc function in default Microkti environments, so we only
 * pass a putc function. We don't need flush in this case, so the third argument
 * is also NULL.
 */
static FILE __stdio = FDEV_SETUP_STREAM(libc_microkit_putc,
                    NULL,
                    NULL,
                    _FDEV_SETUP_WRITE);
FILE *const stdin = &__stdio; __strong_reference(stdin, stdout); __strong_reference(stdin, stderr);

// @ivanv: I could not find a default implementation of `_exit` from picolibc
// (except for one specific arch). This is fairly simple to define and for
// getting printf working we already have to some setup. What we'll probably
// have is some default libc.c that does everything Microkit specific that also
// gets compiled into the final libc.a that gets linked with user-programs.
void _exit (int __status) {
    while (1) {}
}
