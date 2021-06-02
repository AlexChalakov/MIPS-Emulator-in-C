#include "emulator.h"

int main()
{
	if (load_program() < 0)
		return (-1);
	if (make_bytecode() < 0)
		return (-1);
	if (exec_bytecode() < 0)
		return (-1);
	return (0);
}
