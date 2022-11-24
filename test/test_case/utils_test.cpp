#include "gtest/gtest.h"
extern "C" {
#include "utcp/utcp_utils.h"
}

int less(const void* l, const void* r)
{
	return *(int*)l - *(int*)r;
}

int insert(int* arr, size_t max_count, int count, int key)
{
	const int elem_size = sizeof(arr[0]);
	int pos = binary_search(&key, arr, count, elem_size, less);
	if (pos >= 0)
		return 0;
	if (count + 1 > max_count)
		return -1;
	pos = ~pos;

	memmove(arr + pos + 1, arr + pos, (count - pos) * elem_size);
	arr[pos] = key;
	return 1;
}

int remove(int* arr, int count, int key)
{
	const int elem_size = sizeof(arr[0]);
	int pos = binary_search(&key, arr, count, elem_size, less);
	if (pos < 0)
		return 0;

	memmove(arr + pos, arr + pos + 1, (count - pos - 1) * elem_size);
	return 1;
}

TEST(utils, binary_search)
{
	int a[10];
	int cnt = 5;
	for (int i = 0; i < cnt; ++i)
	{
		a[i] = i * 2;
	}
	// {0, 2, 4, 6, 8}

	int key = 6;
	ASSERT_EQ(binary_search(&key, a, std::size(a), sizeof(a[0]), less), 3);
	key = 5;
	ASSERT_EQ(~binary_search(&key, a, std::size(a), sizeof(a[0]), less), 3);
}

TEST(utils, insert)
{
	int a[10];
	int cnt = 5;
	for (int i = 0; i < cnt; ++i)
	{
		a[i] = i * 2;
	}
	// {0, 2, 4, 6, 8}

	for (int i = 0; i < std::size(a); ++i)
	{
		if (i % 2 == 0)
		{
			ASSERT_EQ(insert(a, std::size(a), cnt, i), 0);
		}
		else
		{
			ASSERT_EQ(insert(a, std::size(a), cnt, i), 1);
			cnt++;
		}
	}

	for (int i = 0; i < std::size(a); ++i)
	{
		ASSERT_EQ(a[i], i);
	}
}

TEST(utils, remove)
{
	int a[10];
	int cnt = (int)std::size(a);
	for (int i = 0; i < std::size(a); ++i)
	{
		a[i] = i;
	}

	for (int i = 1; i < std::size(a); i += 2)
	{
		ASSERT_EQ(remove(a, cnt, i), 1);
		cnt--;
	}
	ASSERT_EQ(cnt, 5);
	for (int i = 1; i < cnt; i++)
	{
		ASSERT_EQ(a[i], i * 2);
	}
	for (int i = 1; i < std::size(a); i += 2)
	{
		ASSERT_EQ(remove(a, cnt, i), 0);
	}
	for (int i = 0; i < std::size(a); i += 2)
	{
		ASSERT_EQ(remove(a, cnt, i), 1);
		cnt--;
	}
	ASSERT_EQ(cnt, 0);
}

struct base
{
	int i;
};

struct divide : base
{
	struct iter
	{
		divide* dd;
		int pos;
		
		void operator ++() {
			pos+=2;
		}
		
		bool operator != (const iter& rhs) {
			return pos != rhs.pos;
		}
		
		base* operator* () const
		{
			dd->i = pos;
			return dd;
		}
	};
	
	int start;
	int stop;
	
	iter begin()
	{
		iter ii;
		ii.dd = this;
		ii.pos = start;
		return ii;
	}
	
	iter end()
	{
		iter ii;
		ii.dd = this;
		ii.pos = stop;
		return ii;
	}
};

TEST(utils, foreach)
{
	divide a;
	a.start = 0;
	a.stop = 8;
	
	for (auto b : a)
	{
		printf("%p\t%d\n", b, b->i);
	}
}
