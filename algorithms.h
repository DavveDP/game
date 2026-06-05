#include <std.h>

void partitioning(u32 low, u32 high, bool (*comp)(u32, u32), void (*swap)(u32, u32)) {
    if (low >= high) return;
    if (high - low == 1) {
        if (comp(low, high)) {
            swap(low, high);
        }
        return;
    }

    int partition = high;
    int i, j;
    do {
        i = (int)low;
        j = (int)high;
        while ((i < j) && !comp(i, partition)) {
            i++;
        }
        while ((j > i) && comp(j, partition)) {
            j--;
        }

        if (i < j) {
            swap(i, j);
        }
    } while (i < j);

    swap(i, high);

    if ((i - (int)low) < ((int)high - i)) {
        partitioning(low, i - 1, comp, swap);
        partitioning(i + 1, high, comp, swap);
    } else {
        partitioning(i + 1, high, comp, swap);
        partitioning(low, i - 1, comp, swap);
    }
}

void quicksort(int size, bool (*comp)(u32, u32), void (*swap)(u32, u32)) {
    partitioning(0, size-1, comp, swap);
}
