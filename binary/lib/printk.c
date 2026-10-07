#include <uart.h>
#include <printk.h>

static void
printk_info_init (struct printk_info *info, const char *format)
{
	int i;

	info->format = format;
	for (i = 0; i < PRINTK_BUFFER_SIZE + 1; i++)
		info->buffer[i] = 0;
	info->size = PRINTK_BUFFER_SIZE;
	info->offset = 0;
	/* flag */
	info->flags = 0;
	info->flag.fill = ' ';
	info->flag.width = 0;
	info->flag.length = 0;
}

static void
printk_buffer_write (struct printk_info *info, const char *str, size_t length)
{
	size_t i;

	/* buffer expansion logic is required */
	if (length > info->size - info->offset)
		return;

	for (i = 0; i < length; i++)
		info->buffer[info->offset + i] = str[i];
	info->offset += (uint32_t) length;
}

static void
printk_parse_flags (struct printk_info *info)
{
	int digit;

	info->flags = 0;
	info->flag.fill = ' ';
	info->flag.width = 0;
	info->flag.length = 0;
	info->format++;
	/* set fill from ' ' to '0' */
	if (*info->format == '0')
	{
		info->flag.fill = '0';
		info->format++;
	}
	/* width flag */
	while (*info->format >= '0' && *info->format <= '9')
	{
		info->flags |= PRINTK_FLAG_WIDTH;
		digit = *info->format - '0';
		if (info->flag.width > (PRINTK_MAX_WIDTH - digit) / 10)
			info->flag.width = PRINTK_MAX_WIDTH;
		else
			info->flag.width = info->flag.width * 10 + digit;
		info->format++;
	}
	if (*info->format == 'l')
	{
		info->flag.length = 1;
		info->format++;
		if (*info->format == 'l')
		{
			info->flag.length = 2;
			info->format++;
		}
	}
}

static void
printk_argument_x (struct printk_info *info, uint64_t arg, uint8_t upper)
{
	char buffer[PRINTK_MAX_WIDTH];
	int length;
	int i;

	length = 0;
	do
	{
		buffer[length++] = (upper ? "0123456789ABCDEF" : "0123456789abcdef")[arg % 16];
	}
	while (arg /= 16);
	/* fill with character by width */
	if (info->flags & PRINTK_FLAG_WIDTH)
	{
		if (info->flag.fill == ' ')
		{
			buffer[length++] = 'x';
			buffer[length++] = '0';
			while (length < info->flag.width)
				buffer[length++] = ' ';
		}
		else if (info->flag.fill == '0')
		{
			while (length + 2 < info->flag.width)
				buffer[length++] = '0';
			buffer[length++] = 'x';
			buffer[length++] = '0';
		}
	}
	else
	{
		buffer[length++] = 'x';
		buffer[length++] = '0';
	}
	/* reverse */
	for (i = 0; i < length; i++)
		printk_buffer_write (info, &buffer[length - i - 1], 1);
}

static void
printk_argument_c (struct printk_info *info, char arg)
{
	int i;

	for (i = 1; i < info->flag.width; i++)
		printk_buffer_write (info, " ", 1);
	printk_buffer_write (info, &arg, 1);
}

static void
printk_argument_s (struct printk_info *info, const char *arg)
{
	size_t length;
	size_t i;

	if (!arg)
		arg = "(null)";
	for (length = 0; arg[length]; length++)
		;
	for (i = length; i < (size_t) info->flag.width; i++)
		printk_buffer_write (info, " ", 1);
	printk_buffer_write (info, arg, length);
}

static void
printk_argument_u (struct printk_info *info, uint64_t arg, uint8_t minus)
{
	char buffer[PRINTK_MAX_WIDTH];
	int length;
	int i;

	length = 0;
	do
	{
		buffer[length++] = '0' + arg % 10;
	}
	while (arg /= 10);
	if (info->flag.fill == '0')
	{
		while (length + minus < info->flag.width)
			buffer[length++] = '0';
	}
	if (minus)
		buffer[length++] = '-';
	if (info->flag.fill == ' ')
	{
		while (length < info->flag.width)
			buffer[length++] = ' ';
	}

	for (i = 0; i < length; i++)
		printk_buffer_write (info, &buffer[length - i - 1], 1);
}

static void
printk_argument_d (struct printk_info *info, int64_t arg)
{
	uint8_t minus = arg < 0;
	uint64_t magnitude = (uint64_t) arg;

	if (minus)
		magnitude = UINT64_C(0) - magnitude;
	printk_argument_u (info, magnitude, minus);
}

static uint64_t
printk_read_unsigned (struct printk_info *info, va_list *args)
{
	if (info->flag.length == 2)
		return va_arg (*args, unsigned long long);
	if (info->flag.length == 1)
		return va_arg (*args, unsigned long);
	return va_arg (*args, unsigned int);
}

static int
printk_write_argument (struct printk_info *info, va_list *args)
{
	switch (*info->format)
	{
		case 'x':
		case 'X':
			printk_argument_x (info, printk_read_unsigned (info, args), *info->format == 'X');
			break;

		case 'u':
			printk_argument_u (info, printk_read_unsigned (info, args), 0);
			break;

		case 'd':
			if (info->flag.length == 2)
				printk_argument_d (info, va_arg (*args, long long));
			else if (info->flag.length == 1)
				printk_argument_d (info, va_arg (*args, long));
			else
				printk_argument_d (info, va_arg (*args, int));
			break;

		case 'p':
			if (info->flag.length)
				return 0;
			printk_argument_x (info, (uintptr_t) va_arg (*args, void *), 0);
			break;

		case 'c':
			if (info->flag.length)
				return 0;
			printk_argument_c (info, (char) va_arg (*args, int));
			break;

		case 's':
			if (info->flag.length)
				return 0;
			printk_argument_s (info, va_arg (*args, char *));
			break;

		default:
			return 0;
	}
	++info->format;
	return 1;
}

int
printk (const char *format, ...)
{
	va_list args;
	int result;

	va_start (args, format);
	result = vprintk (format, args);
	va_end (args);
	return result;
}

int
vprintk (const char *format, va_list args)
{
	struct printk_info info;
	va_list copy;
	const char *start;
	uint32_t i;

	va_copy (copy, args);
	printk_info_init (&info, format);

	while (*info.format)
	{
		if (*info.format == '%')
		{
			start = info.format;
			if (info.format[1] == '%')
			{
				printk_buffer_write (&info, "%", 1);
				info.format += 2;
				continue;
			}
			printk_parse_flags (&info);
			if (!printk_write_argument (&info, &copy))
			{
				/* keep unknown/incomplete conversions literal without reading args */
				if (*info.format)
					info.format++;
				while (start < info.format)
					printk_buffer_write (&info, start++, 1);
			}
		}
		else
		{
			printk_buffer_write (&info, info.format, 1);
			info.format++;
		}
	}
	va_end (copy);

	for (i = 0; i < info.offset; i++)
		uart_putc (info.buffer[i]);

	return (int) info.offset;
}

