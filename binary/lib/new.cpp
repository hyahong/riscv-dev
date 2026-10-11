#include <new>
#include <malloc.h>
#include <printk.h>

namespace std {
	const nothrow_t nothrow {};
}

namespace {

void *
require_allocation (void *ptr)
{
	if (ptr)
		return ptr;

	// exceptions are disabled in this bare-metal runtime.
	printk ("new: allocation failed\n");
	for (;;)
		asm volatile ("wfi");
}

}

void *
operator new (std::size_t size)
{
	return require_allocation (malloc (size ? size : 1));
}

void *
operator new[] (std::size_t size)
{
	return ::operator new (size);
}

void
operator delete (void *ptr) noexcept
{
	free (ptr);
}

void
operator delete[] (void *ptr) noexcept
{
	::operator delete (ptr);
}

void
operator delete (void *ptr, std::size_t) noexcept
{
	::operator delete (ptr);
}

void
operator delete[] (void *ptr, std::size_t) noexcept
{
	::operator delete (ptr);
}

void *
operator new (std::size_t size, const std::nothrow_t &) noexcept
{
	return malloc (size ? size : 1);
}

void *
operator new[] (std::size_t size, const std::nothrow_t &tag) noexcept
{
	return ::operator new (size, tag);
}

void
operator delete (void *ptr, const std::nothrow_t &) noexcept
{
	::operator delete (ptr);
}

void
operator delete[] (void *ptr, const std::nothrow_t &) noexcept
{
	::operator delete (ptr);
}

// Newlib's memalign uses the same malloc heap and is released with free.
void *
operator new (std::size_t size, std::align_val_t alignment)
{
	return require_allocation (memalign (static_cast<std::size_t> (alignment), size ? size : 1));
}

void *
operator new[] (std::size_t size, std::align_val_t alignment)
{
	return ::operator new (size, alignment);
}

void
operator delete (void *ptr, std::align_val_t) noexcept
{
	::operator delete (ptr);
}

void
operator delete[] (void *ptr, std::align_val_t) noexcept
{
	::operator delete (ptr);
}

void
operator delete (void *ptr, std::size_t, std::align_val_t) noexcept
{
	::operator delete (ptr);
}

void
operator delete[] (void *ptr, std::size_t, std::align_val_t) noexcept
{
	::operator delete (ptr);
}

void *
operator new (std::size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept
{
	return memalign (static_cast<std::size_t> (alignment), size ? size : 1);
}

void *
operator new[] (std::size_t size, std::align_val_t alignment, const std::nothrow_t &tag) noexcept
{
	return ::operator new (size, alignment, tag);
}

void
operator delete (void *ptr, std::align_val_t, const std::nothrow_t &) noexcept
{
	::operator delete (ptr);
}

void
operator delete[] (void *ptr, std::align_val_t, const std::nothrow_t &) noexcept
{
	::operator delete (ptr);
}
