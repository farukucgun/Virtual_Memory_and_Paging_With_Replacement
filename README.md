# Virtual_Memory_and_Paging_With_Replacement

Simulate memory accesses and paging behaviour

Supports single and two level paging

Implemented FIFO, LRU, CLOCK and ECLOCK algorithms for page replacement

1. Read the reference made (type, address, value) from a txt file
2. Translate the virtual address to physical address
3. In case of page fault, either load the page into empty frame if any or use a page replacement algorithm to make space
4. Perform the read or write operation and adjust the R, M, V bits in the respective page table entry
5. After all the input file is processed, write the pages in physical memory to their locations in the backing store

Keywords: paging, virtual memory, physical memory, virtual addresses, physical addresses, address translation, 
page replacement algorithms, single-level and two-level paging, backing store, swap space, random file I/O
