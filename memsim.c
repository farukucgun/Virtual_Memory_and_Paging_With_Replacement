#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>

// Global variables

int level; // number of levels in the page table, 1 <= level <= 2
char addrfile[64]; // name of the file containing the memory references (virtual addresses)
char swapfile[64]; // name of the file containing the backing store (swap space), size = virtual memory size (64 KB), 
// 1024 pages, size of each page = 64 bytes. If it doesn't exist, create it and initialize it to all 0s.
int fcount; // number of frames in the physical memory, 4 <= fcount <= 128
int PAGE_SIZE = 64; // size of each page in bytes
char algo[64]; // name of the page replacement algorithm: FIFO, LRU, CLOCK, ECLOCK
int tick; // timer tick period in number of memory references done
char outfile[64]; // name of the file containing the output of the simulation
int lru_order[128]; // LRU order of the frames in the physical memory
int fifo_order[128]; // FIFO order of the frames in the physical memory
int clock_hand_order[128]; // CLOCK order of the frames in the physical memory
int eclock_hand_order[128]; // ECLOCK order of the frames in the physical memory
int next_empty_frame = 0;
// Structs

// Page table entry
typedef struct { // 16 bits --> square root of fcount bits frame number, referenced bit, modified bit
    uint16_t frame : 13; // Frame number, now 13 bits
    uint16_t r : 1;      // Referenced bit
    uint16_t m : 1;      // Modified bit
    uint16_t v : 1;      // Valid bit
} PTE;

// Page table
typedef struct {
    PTE *entries; // array of page table entries
    int size; // size of the page table
} PT;

// Frame
typedef struct {
    uint8_t data[64]; // Data stored in the frame
} Frame;

// Physical memory
typedef struct {
    Frame *frames; // array of frames
    int size; // size of the physical memory
} PM;

// Page
typedef struct {
    uint8_t data[64]; // Data stored in the page
} Page;

// Virtual memory
typedef struct {
    Page *pages; // array of pages
    int size; // size of the virtual memory
} VM;

// Memory reference
typedef struct {
    char type; // type of memory reference: r (read), w (write)
    int addr; // virtual address
    int value; // value to write (if type is w)
} Ref;


// Function prototypes

// Read the command line arguments
void read_args(int argc, char *argv[]);
// Read the memory references (virtual addresses) from the address file
void read_refs(char *addrfile, Ref *refs, int *ref_count);
// Initialize the page table
void init_pt(PT *pt, int levels);
// Initialize the physical memory
void init_pm(PM *pm);
// Initialize the virtual memory
void init_vm(VM *vm, int levels);
// Initialize the backing store
void init_bs(char *swapfile);
// Write the physical memory to the backing store
void write_pm_to_swap(PM *pm);
// update the LRU order
void update_lru_order(int vpn, int pm_size, int levels);


// Main function
int main(int argc, char *argv[]) {
    // Read the command line arguments
    read_args(argc, argv);

    // Read the memory references (virtual addresses) from the address file
    Ref *refs = malloc(1000000 * sizeof(Ref));  // Allocate memory for the array of memory references
    int ref_count;  // Number of memory references
    read_refs(addrfile, refs, &ref_count);

    // Initialize the page table
    PT pt;
    init_pt(&pt, level);

    // Initialize the physical memory
    PM pm;
    init_pm(&pm);

    // Initialize the virtual memory
    VM vm;
    init_vm(&vm, level);

    // Initialize the backing store
    init_bs(swapfile);
    
    // Open the swap file in read/write mode
    FILE *swap_file = fopen(swapfile, "rb+");  

    // Open the output file in write mode
    FILE *out_file = fopen(outfile, "w");

    // Initialize the page fault counter
    int pfault_count = 0;

    // Initialize the timer
    int timer = 0;

    // Initialize the clock hand
    int clock_hand = 0;

    // Initialize the enhanced clock hand
    int eclock_hand = 0;

    // Initialize the LRU order
    for (int i = 0; i < pm.size; i++) {
        lru_order[i] = 0;
    }

    // print the LRU order
    printf("lru_order before simulation:\n");
    for (int i = 0; i < pm.size; i++) {
        printf("lru_order[%d]: %d\n", i, lru_order[i]);
    }

    // create an array that holds 32 page tables and initialize them to null page tables 
    PT pt_array[32];
    for (int i = 0; i < 32; i++) {
        pt_array[i].entries = NULL;
        pt_array[i].size = 0;
    }

    // simulating the memory references
    for (int i = 0; i < ref_count; i++) {

        // clear the R bits in the page table entries every tick memory references
        if (i != 0 && i % tick == 0) {
            for (int j = 0; j < pt.size; j++) {
                pt.entries[j].r = 0;
            }
        }

        Ref ref = refs[i];  // Get the current memory reference

        // write the memory reference to the output file
        fprintf(out_file, "ADDR:0x%04x ", ref.addr);

        // Translate the virtual address to a physical address
        int vpn = ref.addr >> 6;  // virtual page number
        int offset = ref.addr & 0x3f;  // offset
        // If single-level paging is used, then PTE1 will be the index of the single-level page table entry used in translation.
        int pte1 = (level == 1) ? vpn : (vpn >> 5);  // page table entry 1
        // int pte1 = vpn >> 5;  // page table entry 1
        int pte2 = (level == 1) ? 0 : (vpn & 0x1f);  // page table entry 2
        int pfn = -1;  // physical frame number
        int pa = -1;  // physical address

        int pageFault = 0;  // page fault flag

        // write the page table entries and the offset to the output file
        fprintf(out_file, "PTE1:0x%01x ", pte1);
        fprintf(out_file, "PTE2:0x%01x ", pte2);
        fprintf(out_file, "offset:0x%01x ", offset);

        printf("vpn: %d\n", vpn);
        printf("offset: %d\n", offset);
        printf("pte1: %d\n", pte1);
        printf("pte2: %d\n", pte2);
        printf("pt.entries[vpn].v: %d\n", pt.entries[vpn].v);

        if (level == 1) {
            // Single-level paging
            if (pt.entries[vpn].v == 0) {
                // Page fault
                pageFault = 1;
                pfault_count++;
                // Load the page from the backing store
                fseek(swap_file, vpn * PAGE_SIZE, SEEK_SET);  // Seek to the page in the swap file
                Page page;  // Page to be loaded
                fread(&page, sizeof(Page), 1, swap_file);  // Read the page from the swap fill
                
                if (next_empty_frame >= pm.size) {
                    printf("Error: No empty frame\n");
                    // No empty frame
                    // Page replacement
                    if (strcmp(algo, "FIFO") == 0) {
                        printf("FIFO algorithm runs\n");
                        // FIFO
                        // Select the victim page
                        int victim_page = fifo_order[0];
                        printf("victim_page: %d\n", victim_page);

                        // find the frame number associated with the victim page
                        int victim_frame = pt.entries[victim_page].frame;
                        printf("victim_frame: %d\n", victim_frame);

                        // Write the victim page to the backing store if it is modified
                        if (pt.entries[victim_page].m == 1) {
                            fseek(swap_file, victim_page * PAGE_SIZE, SEEK_SET);  // Seek to the page in the swap file
                            fwrite(&pm.frames[victim_frame], sizeof(Frame), 1, swap_file);  // Write the page to the swap file
                        }

                        // Load the page from the backing store
                        fseek(swap_file, victim_page * PAGE_SIZE, SEEK_SET);  // Seek to the page in the swap file
                        Page page;  // Page to be loaded
                        fread(&page, sizeof(Page), 1, swap_file);  // Read the page from the swap file

                        // Update the page table
                        pt.entries[vpn].frame = victim_frame;
                        pt.entries[vpn].r = 1;
                        pt.entries[vpn].m = 0;
                        pt.entries[vpn].v = 1;

                        // update the physical memory
                        for(int i = 1; i < PAGE_SIZE; i++) {
                            pm.frames[victim_frame].data[i] = page.data[i];
                        }

                        // Update the FIFO order
                        for (int i = 0; i < pm.size - 1; i++) {
                            fifo_order[i] = fifo_order[i + 1];
                        }

                        fifo_order[pm.size - 1] = vpn;
                        pfn = victim_frame;
                        pa = pfn * PAGE_SIZE + offset;

                    } else if (strcmp(algo, "LRU") == 0) {
                        printf("LRU algorithm runs\n");
                        // LRU
                        // Select the victim page
                        int victim_page = lru_order[pm.size - 1];
                        printf("victim_page: %d\n", victim_page);

                        // find the frame number associated with the victim page
                        int victim_frame = pt.entries[victim_page].frame;
                        printf("victim_frame: %d\n", victim_frame);

                        // Write the victim page to the backing store if it is modified
                        if (pt.entries[victim_page].m == 1) {
                            fseek(swap_file, victim_page * PAGE_SIZE, SEEK_SET);  // Seek to the page in the swap file
                            fwrite(&pm.frames[victim_frame], sizeof(Frame), 1, swap_file);  // Write the page to the swap file
                        }

                        // Load the page from the backing store
                        fseek(swap_file, victim_page * PAGE_SIZE, SEEK_SET);  // Seek to the page in the swap file
                        Page page;  // Page to be loaded
                        fread(&page, sizeof(Page), 1, swap_file);  // Read the page from the swap file

                        // Update the page table
                        pt.entries[vpn].frame = victim_frame;
                        pt.entries[vpn].r = 1;
                        pt.entries[vpn].m = 0;
                        pt.entries[vpn].v = 1;

                        // update the physical memory
                        for(int i = 1; i < PAGE_SIZE; i++) {
                            pm.frames[victim_frame].data[i] = page.data[i];
                        }

                        // Update the LRU order
                        update_lru_order(vpn, pm.size, level);

                        pfn = victim_frame;
                        pa = pfn * PAGE_SIZE + offset;
                    } else if (strcmp(algo, "CLOCK") == 0) {
                        printf("CLOCK algorithm runs\n");
                        // CLOCK

                        // Select the victim page
                        int victim_page = -1;
                        int found = 0;
                        while (!found) {
                            if (pt.entries[clock_hand_order[clock_hand]].r == 0) {
                                victim_page = clock_hand_order[clock_hand];
                                found = 1;
                            } else {
                                pt.entries[clock_hand_order[clock_hand]].r = 0;
                                clock_hand = (clock_hand + 1) % pm.size;
                            }
                        }

                        printf("victim_page: %d\n", victim_page);

                        // find the frame number associated with the victim page
                        int victim_frame = pt.entries[victim_page].frame;
                        printf("victim_frame: %d\n", victim_frame);

                        // Write the victim page to the backing store if it is modified
                        if (pt.entries[victim_page].m == 1) {
                            fseek(swap_file, victim_page * PAGE_SIZE, SEEK_SET);  // Seek to the page in the swap file
                            fwrite(&pm.frames[victim_frame], sizeof(Frame), 1, swap_file);  // Write the page to the swap file
                        }

                        // Load the page from the backing store
                        fseek(swap_file, victim_page * PAGE_SIZE, SEEK_SET);  // Seek to the page in the swap file
                        Page page;  // Page to be loaded
                        fread(&page, sizeof(Page), 1, swap_file);  // Read the page from the swap file

                        // Update the page table
                        pt.entries[vpn].frame = victim_frame;
                        pt.entries[vpn].r = 1;
                        pt.entries[vpn].m = 0;
                        pt.entries[vpn].v = 1;

                        // update the physical memory
                        for(int i = 1; i < PAGE_SIZE; i++) {
                            pm.frames[victim_frame].data[i] = page.data[i];
                        }

                        // Update the CLOCK order
                        for (int i = 0; i < pm.size - 1; i++) {
                            clock_hand_order[i] = clock_hand_order[i + 1];
                        }

                        clock_hand_order[pm.size - 1] = vpn;

                        pfn = victim_frame;
                        pa = pfn * PAGE_SIZE + offset;
                    } else if (strcmp(algo, "ECLOCK") == 0) {
                        printf("ECLOCK algorithm runs\n");
                        // ECLOCK

                        // Select the victim page
                        int victim_page = -1;
                        int found = 0;

                        printf("eclock_hand1: %d\n", eclock_hand);

                        // Step 1                        
                        for (int i = 0; i <= pm.size -1; i++){
                            printf("eclock_hand1: %d\n", eclock_hand);
                            printf("i: %d\n", i);
                            if (pt.entries[eclock_hand_order[eclock_hand]].r == 0 && pt.entries[eclock_hand_order[eclock_hand]].m == 0) {
                                victim_page = eclock_hand_order[eclock_hand];
                                found = 1;
                                printf("victim_page_found1: %d\n", victim_page);
                                printf("eclock_hand1found: %d\n", eclock_hand);
                                eclock_hand = (eclock_hand + 1) % pm.size;
                                break;
                            } 
                            eclock_hand = (eclock_hand + 1) % pm.size;
                        }

                        printf("eclock_hand2: %d\n", eclock_hand);

                        // Step 2
                        if (victim_page == -1) {
                            for (int i = 0; i <= pm.size -1; i++){
                                if (pt.entries[eclock_hand_order[eclock_hand]].r == 0 && pt.entries[eclock_hand_order[eclock_hand]].m == 1) {
                                    victim_page = eclock_hand_order[eclock_hand];
                                    found = 1;
                                    printf("victim_page_found2: %d\n", victim_page);
                                    printf("eclock_hand2found: %d\n", eclock_hand);
                                    eclock_hand = (eclock_hand + 1) % pm.size;
                                    break;
                                } else if (pt.entries[eclock_hand_order[eclock_hand]].r == 1) {
                                    pt.entries[eclock_hand_order[eclock_hand]].r = 0;
                                }
                                eclock_hand = (eclock_hand + 1) % pm.size;
                            }
                        }

                        printf("eclock_hand3: %d\n", eclock_hand);

                        // Step 3

                        if (victim_page == -1) {
                            for(int i = 0; i < pm.size-1; i++){
                                if (pt.entries[eclock_hand_order[eclock_hand]].r == 0 && pt.entries[eclock_hand_order[eclock_hand]].m == 0) {
                                    victim_page = eclock_hand_order[eclock_hand];
                                    found = 1;
                                    printf("victim_page_found3: %d\n", victim_page);
                                    printf("eclock_hand3found: %d\n", eclock_hand);
                                    eclock_hand = (eclock_hand + 1) % pm.size;
                                    break;
                                }
                                eclock_hand = (eclock_hand + 1) % pm.size;
                            }
                        }
                        
                        printf("eclock_hand4: %d\n", eclock_hand);

                        // Step 4

                        if (victim_page == -1) {
                            for(int i = 0; i < pm.size-1; i++){
                                if (pt.entries[eclock_hand_order[eclock_hand]].r == 0 && pt.entries[eclock_hand_order[eclock_hand]].m == 1) {
                                    victim_page = eclock_hand_order[eclock_hand];
                                    found = 1;
                                    printf("victim_page_found4: %d\n", victim_page);
                                    printf("eclock_hand4found: %d\n", eclock_hand);
                                    eclock_hand = (eclock_hand + 1) % pm.size;
                                    break;
                                }
                                eclock_hand = (eclock_hand + 1) % pm.size; 
                            }
                        }

                        printf("victim_page: %d\n", victim_page);

                        // find the frame number associated with the victim page
                        int victim_frame = pt.entries[victim_page].frame;
                        printf("victim_frame: %d\n", victim_frame);

                        // Write the victim page to the backing store if it is modified
                        if (pt.entries[victim_page].m == 1) {
                            fseek(swap_file, victim_page * PAGE_SIZE, SEEK_SET);  // Seek to the page in the swap file
                            fwrite(&pm.frames[victim_frame], sizeof(Frame), 1, swap_file);  // Write the page to the swap file
                        }

                        // Load the page from the backing store
                        fseek(swap_file, victim_page * PAGE_SIZE, SEEK_SET);  // Seek to the page in the swap file
                        Page page;  // Page to be loaded
                        fread(&page, sizeof(Page), 1, swap_file);  // Read the page from the swap file

                        // Update the page table
                        pt.entries[vpn].frame = victim_frame;
                        pt.entries[vpn].r = 1;
                        pt.entries[vpn].m = 0;
                        pt.entries[vpn].v = 1;

                        // update the physical memory
                        for(int i = 1; i < PAGE_SIZE; i++) {
                            pm.frames[victim_frame].data[i] = page.data[i];
                        }

                        // Update the ECLOCK order
                        for (int i = 0; i < pm.size - 1; i++) {
                            eclock_hand_order[i] = eclock_hand_order[i + 1];
                        }

                        eclock_hand_order[pm.size - 1] = vpn;

                        pfn = victim_frame;
                        pa = pfn * PAGE_SIZE + offset;                        
                    } else {
                        printf("Error: Wrong page replacement algorithm\n");
                        exit(1);
                    }

                } else {
                    // Empty frame found
                    // Load the page into the empty frame
                    printf("empty frame found\n");
                    int empty_frame = next_empty_frame;
                    next_empty_frame++;

                    Frame frame;
                    for(int i = 0; i < PAGE_SIZE; i++) {
                        frame.data[i] = page.data[i];
                    }
                    pm.frames[empty_frame] = frame;
                    // Update the page table
                    printf("----------------vpn: %d\n", vpn);
                    printf("empty_frame: %d\n", empty_frame);
                    printf("pt.entries[vpn].frame: %d\n", pt.entries[vpn].frame);
                    printf("pt.entries[vpn].r: %d\n", pt.entries[vpn].r);
                    printf("pt.entries[vpn].m: %d\n", pt.entries[vpn].m);
                    printf("pt.entries[vpn].v: %d\n", pt.entries[vpn].v);
                    printf("----------------ref.addr: %d\n", ref.addr);
                    pt.entries[vpn].frame = empty_frame;
                    pt.entries[vpn].r = 1;
                    pt.entries[vpn].m = 0;
                    pt.entries[vpn].v = 1;
                    // Update the physical frame number
                    pfn = empty_frame;

                    // Update the FIFO order
                    for (int i = 0; i < pm.size - 1; i++) {
                        fifo_order[i] = fifo_order[i + 1];
                    }

                    fifo_order[pm.size - 1] = vpn;
                    
                    // Update the LRU order, if vpn is already in the order, change its position to the first
                    update_lru_order(vpn, pm.size, level);
                }
                
            } else {
                // Page hit
                printf("Page hit\n");
                // Update the R bit
                pt.entries[vpn].r = 1;
                // Update the physical frame number
                printf("before pfn update\n");
                printf("pfn: %d\n", pfn);
                printf("pt.entries[vpn].frame: %d\n", pt.entries[vpn].frame);
                pfn = pt.entries[vpn].frame;
                // get the data
                Frame frame = pm.frames[pfn];
                printf("frame.data[offset]: %d\n", frame.data[offset]);
                // Update the clock hand
                clock_hand = (clock_hand + 1) % pm.size; // ADDED LATER
                // Update the ECLOCK hand
                // eclock_hand = (eclock_hand + 1) % pm.size; // ADDED LATER
                // Update the LRU order, if vpn is already in the order, change its position to the first
                update_lru_order(vpn, pm.size, level);
            }
            // Update the physical address
            pa = pfn * PAGE_SIZE + offset;

            // Write the data to the physical address if the memory reference is a write operation
            if (ref.type == 'w') {
                printf("writing to pm.frames[pfn].data[offset]: %d the value: %d\n", pm.frames[pfn].data[offset], ref.value); 
                printf("pfn: %d\n", pfn);
                printf("offset: %d\n", offset);
                pm.frames[pfn].data[offset] = ref.value;
                // Update the M bit
                pt.entries[vpn].m = 1;
            }
        
        } else if (level == 2) {
            printf("level 2\n");
            // Two-level paging
            if (pt_array[pte1].entries == NULL) {
                // initilize the inner page table
                init_pt(&pt_array[pte1], level);
            }

            if (pt_array[pte1].entries[pte2].v == 0) {
                // Page fault
                pageFault = 1;
                pfault_count++;
                // Load the page from the backing store
                fseek(swap_file, pte2 * PAGE_SIZE, SEEK_SET);  // Seek to the page in the swap file
                Page page;  // Page to be loaded
                fread(&page, sizeof(Page), 1, swap_file);  // Read the page from the swap file
                
                if (next_empty_frame >= pm.size) {
                    printf("Error: No empty frame\n");
                    // No empty frame
                    // Page replacement
                    if (strcmp(algo, "FIFO") == 0) {
                        printf("FIFO algorithm runs\n");
                        // FIFO
                        // Select the victim page
                        int victim_page = fifo_order[0];
                        printf("victim_page: %d\n", victim_page);

                        // find the frame number associated with the victim page
                        int victim_frame = pt_array[pte1].entries[victim_page].frame;
                        printf("victim_frame: %d\n", victim_frame);

                        // Write the victim page to the backing store if it is modified
                        if (pt_array[pte1].entries[victim_page].m == 1) {
                            fseek(swap_file, victim_page * PAGE_SIZE, SEEK_SET);  // Seek to the page in the swap file
                            fwrite(&pm.frames[victim_frame], sizeof(Frame), 1, swap_file);  // Write the page to the swap file
                        }

                        // Load the page from the backing store
                        fseek(swap_file, victim_page * PAGE_SIZE, SEEK_SET);  // Seek to the page in the swap file
                        Page page;  // Page to be loaded
                        fread(&page, sizeof(Page), 1, swap_file);  // Read the page from the swap file

                        // Update the page table
                        pt_array[pte1].entries[pte2].frame = victim_frame;
                        pt_array[pte1].entries[pte2].r = 1;
                        pt_array[pte1].entries[pte2].m = 0;
                        pt_array[pte1].entries[pte2].v = 1;

                        // update the physical memory
                        for(int i = 1; i < PAGE_SIZE; i++) {
                            pm.frames[victim_frame].data[i] = page.data[i];
                        }

                        // Update the FIFO order
                        for (int i = 0; i < pm.size - 1; i++) {
                            fifo_order[i] = fifo_order[i + 1];
                        }

                        fifo_order[pm.size - 1] = pte2;
                        pfn = victim_frame;
                        pa = pfn * PAGE_SIZE + offset;

                    } else if (strcmp(algo, "LRU") == 0) {
                        printf("LRU algorithm runs\n");
                        // LRU
                        // Select the victim page
                        int victim_page = lru_order[pm.size - 1];
                        printf("victim_page: %d\n", victim_page);

                        // find the frame number associated with the victim page
                        int victim_frame = pt_array[pte1].entries[victim_page].frame;
                        printf("victim_frame: %d\n", victim_frame);

                        // Write the victim page to the backing store if it is modified
                        if (pt_array[pte1].entries[victim_page].m == 1) {
                            fseek(swap_file, victim_page * PAGE_SIZE, SEEK_SET);  // Seek to the page in the swap file
                            fwrite(&pm.frames[victim_frame], sizeof(Frame), 1, swap_file);  // Write the page to the swap file
                        }

                        // Load the page from the backing store
                        fseek(swap_file, victim_page * PAGE_SIZE, SEEK_SET);  // Seek to the page in the swap file
                        Page page;  // Page to be loaded
                        fread(&page, sizeof(Page), 1, swap_file);  // Read the page from the swap file

                        // Update the page table
                        pt_array[pte1].entries[pte2].frame = victim_frame;
                        pt_array[pte1].entries[pte2].r = 1;
                        pt_array[pte1].entries[pte2].m = 0;
                        pt_array[pte1].entries[pte2].v = 1;

                        // update the physical memory
                        for(int i = 1; i < PAGE_SIZE; i++) {
                            pm.frames[victim_frame].data[i] = page.data[i];
                        }

                        // Update the LRU order
                        update_lru_order(pte2, pm.size, level);

                        pfn = victim_frame;
                        pa = pfn * PAGE_SIZE + offset;
                    } else if (strcmp(algo, "CLOCK") == 0) {
                        // CLOCK

                        // Select the victim page
                        int victim_page = -1;
                        int found = 0;
                        while (!found) {
                            if (pt_array[pte1].entries[clock_hand_order[clock_hand]].r == 0) {
                                victim_page = clock_hand_order[clock_hand];
                                found = 1;
                            } else {
                                pt_array[pte1].entries[clock_hand_order[clock_hand]].r = 0;
                                clock_hand = (clock_hand + 1) % pm.size;
                            }
                        }

                        printf("victim_page: %d\n", victim_page);

                        // find the frame number associated with the victim page
                        int victim_frame = pt_array[pte1].entries[victim_page].frame;
                        printf("victim_frame: %d\n", victim_frame);

                        // Write the victim page to the backing store if it is modified
                        if (pt_array[pte1].entries[victim_page].m == 1) {
                            fseek(swap_file, victim_page * PAGE_SIZE, SEEK_SET);  // Seek to the page in the swap file
                            fwrite(&pm.frames[victim_frame], sizeof(Frame), 1, swap_file);  // Write the page to the swap file
                        }

                        // Load the page from the backing store
                        fseek(swap_file, victim_page * PAGE_SIZE, SEEK_SET);  // Seek to the page in the swap file
                        Page page;  // Page to be loaded
                        fread(&page, sizeof(Page), 1, swap_file);  // Read the page from the swap file

                        // Update the page table
                        pt_array[pte1].entries[pte2].frame = victim_frame;
                        pt_array[pte1].entries[pte2].r = 1;
                        pt_array[pte1].entries[pte2].m = 0;
                        pt_array[pte1].entries[pte2].v = 1;

                        // update the physical memory
                        for(int i = 1; i < PAGE_SIZE; i++) {
                            pm.frames[victim_frame].data[i] = page.data[i];
                        }

                        // Update the CLOCK order
                        for (int i = 0; i < pm.size - 1; i++) {
                            clock_hand_order[i] = clock_hand_order[i + 1];
                        }

                        clock_hand_order[pm.size - 1] = pte2;

                        pfn = victim_frame;
                        pa = pfn * PAGE_SIZE + offset;
                    } else if (strcmp(algo, "ECLOCK") == 0) {
                        printf("ECLOCK algorithm runs\n");
                        // ECLOCK
                        
                    } else {
                        printf("Error: Wrong page replacement algorithm\n");
                        exit(1);
                    }

                } else {
                    // Empty frame found
                    // Load the page into the empty frame
                    printf("empty frame found\n");
                    int empty_frame = next_empty_frame;
                    next_empty_frame++;

                    Frame frame;
                    for(int i = 0; i < PAGE_SIZE; i++) {
                        frame.data[i] = page.data[i];
                    }
                    pm.frames[empty_frame] = frame;
                    // Update the page table
                    printf("----------------pte2: %d\n", pte2);
                    printf("empty_frame: %d\n", empty_frame);
                    printf("pt_array[pte1].entries[pte2].frame: %d\n", pt_array[pte1].entries[pte2].frame);
                    printf("pt_array[pte1].entries[pte2].r: %d\n", pt_array[pte1].entries[pte2].r);
                    printf("pt_array[pte1].entries[pte2].m: %d\n", pt_array[pte1].entries[pte2].m);
                    printf("pt_array[pte1].entries[pte2].v: %d\n", pt_array[pte1].entries[pte2].v);
                    printf("----------------ref.addr: %d\n", ref.addr);
                    pt_array[pte1].entries[pte2].frame = empty_frame;
                    pt_array[pte1].entries[pte2].r = 1;
                    pt_array[pte1].entries[pte2].m = 0;
                    pt_array[pte1].entries[pte2].v = 1;
                    // Update the physical frame number
                    pfn = empty_frame;

                    // Update the FIFO order
                    for (int i = 0; i < pm.size - 1; i++) {
                        fifo_order[i] = fifo_order[i + 1];
                    }

                    fifo_order[pm.size - 1] = pte2;
                    
                    // Update the LRU order, if vpn is already in the order, change its position to the first
                    update_lru_order(pte2, pm.size, level);
                }
                
            } else {
                // Page hit
                // Update the R bit
                pt_array[pte1].entries[pte2].r = 1;
                // Update the physical frame number
                pfn = pt_array[pte1].entries[pte2].frame;
                // get the data
                Frame frame = pm.frames[pfn];
                printf("frame.data[offset]: %d\n", frame.data[offset]);
                // Update the clock hand
                clock_hand = (clock_hand + 1) % pm.size; // ADDED LATER

                // Update the LRU order, if vpn is already in the order, change its position to the first
                update_lru_order(pte2, pm.size, level);
            }
            // Update the physical address
            pa = pfn * PAGE_SIZE + offset;
            
            // Write the data to the physical address if the memory reference is a write operation
            if (ref.type == 'w') {
                printf("writing to pm.frames[pfn].data[offset]: %d the value: %d\n", pm.frames[pfn].data[offset], ref.value); 
                printf("pfn: %d\n", pfn);
                printf("offset: %d\n", offset);
                pm.frames[pfn].data[offset] = ref.value;
                // Update the M bit
                pt_array[pte1].entries[pte2].m = 1;
            }
            
        } else {
            printf("Error: Wrong number of levels in the page table\n");
            exit(1);
        }

        // Write the physical address and the phyiscal frame number to the output file
        fprintf(out_file, "PFN:0x%x PA:0x%04x ", pfn, pa);
        
        // Write the page fault flag to the output file
        if(pageFault == 1){
            fprintf(out_file, "pgfault\n");
        }else{
            fprintf(out_file, " \n");
        }

    }

    // Write the page fault counter to the output file
    fprintf(out_file, "%d\n", pfault_count);

    // Write the physical memory to the backing store
    write_pm_to_swap(&pm);

    // close the output file
    fclose(out_file);

    // Free the memory
    free(refs);
    free(pt.entries);
    free(pm.frames);
    free(vm.pages);

    // Return
    return 0;
}


// Function definitions

// Read the command line arguments
void read_args(int argc, char *argv[]) {
    // Check the number of arguments
    if (argc != 15) {
        printf("Error: Wrong number of arguments\n");
        exit(1);
    }
    // Read the arguments
    for (int i = 1; i < argc; i += 2) {
        if (strcmp(argv[i], "-p") == 0) {
            level = atoi(argv[i + 1]);
        } else if (strcmp(argv[i], "-r") == 0) {
            strcpy(addrfile, argv[i + 1]);
        } else if (strcmp(argv[i], "-s") == 0) {
            strcpy(swapfile, argv[i + 1]);
        } else if (strcmp(argv[i], "-f") == 0) {
            fcount = atoi(argv[i + 1]);
        } else if (strcmp(argv[i], "-a") == 0) {
            strcpy(algo, argv[i + 1]);
        } else if (strcmp(argv[i], "-t") == 0) {
            tick = atoi(argv[i + 1]);
        } else if (strcmp(argv[i], "-o") == 0) {
            strcpy(outfile, argv[i + 1]);
        } else {
            printf("Error: Wrong argument\n");
            exit(1);
        }
    }
    // Check the arguments
    if (level < 1 || level > 2) {
        printf("Error: Wrong number of levels in the page table\n");
        exit(1);
    }
    if (fcount < 4 || fcount > 128) {
        printf("Error: Wrong number of frames in the physical memory\n");
        exit(1);
    }
    if (strcmp(algo, "FIFO") != 0 && strcmp(algo, "LRU") != 0 && strcmp(algo, "CLOCK") != 0 && strcmp(algo, "ECLOCK") != 0) {
        printf("Error: Wrong page replacement algorithm\n");
        exit(1);
    }

    printf("level = %d\n", level);
    printf("addrfile = %s\n", addrfile);
    printf("swapfile = %s\n", swapfile);
    printf("fcount = %d\n", fcount);
    printf("algo = %s\n", algo);
    printf("tick = %d\n", tick);
    printf("outfile = %s\n", outfile);
}

// Read the memory references (virtual addresses) from the address file
void read_refs(char *addrfile, Ref *refs, int *ref_count) {
    FILE *addr_file = fopen(addrfile, "r");  // Open the address file in read mode
    if (addr_file == NULL) {
        printf("Error: Address file does not exist\n");
        exit(1);
    }

    char line[64];  // Line of the address file
    int i = 0;  // Index of the array of memory references
    while (fgets(line, sizeof(line), addr_file)) {
        if (line[0] == 'r') {
            refs[i].type = 'r';
            sscanf(line, "%*c %x", &refs[i].addr);
        } else if (line[0] == 'w') {
            refs[i].type = 'w';
            sscanf(line, "%*c %x %x", &refs[i].addr, &refs[i].value);
        } else {
            printf("Error: Wrong memory reference type\n");
            exit(1);
        }
        i++;
    }
    *ref_count = i;  // Set the number of memory references

    fclose(addr_file);  // Close the address file

    printf("ref_count = %d\n", *ref_count);

    // Print the memory references
    for (int i = 0; i < *ref_count; i++) {
        printf("%c %x %x\n", refs[i].type, refs[i].addr, refs[i].value);
    }
}    

// Initialize the page table
void init_pt(PT *pt, int levels) {
    pt->size = (1 << (10 - 2 * (levels - 1)));
    pt->entries = malloc(pt->size * sizeof(PTE));
    for (int i = 0; i < pt->size; i++) {
        pt->entries[i].frame = 0;
        pt->entries[i].r = 0;
        pt->entries[i].m = 0;
        pt->entries[i].v = 0;
    }
}

// Initialize the physical memory
void init_pm(PM *pm) {
    pm->size = fcount;
    pm->frames = malloc(pm->size * PAGE_SIZE);
    for (int i = 0; i < pm->size; i++) {
        for (int j = 0; j < PAGE_SIZE; j++) {
            pm->frames[i].data[j] = 0;
        }
        clock_hand_order[i] = i;
        eclock_hand_order[i] = i;
    }
}

// Initialize the virtual memory
void init_vm(VM *vm, int levels) {
    vm->size = (1 << (10 - 2 * (levels - 1)));
    vm->pages = malloc(vm->size * PAGE_SIZE);
    for (int i = 0; i < vm->size; i++) {
        for (int j = 0; j < PAGE_SIZE; j++) {
            vm->pages[i].data[j] = 0;
        }
    }
}

// Initialize the backing store, create it if doesn't exist and initialize it to all 0s
void init_bs(char *swapfile) {
    FILE *swap_file = fopen(swapfile, "rb");  // Open the swap file in write mode
    if (swap_file == NULL) {
        // Create the swap file
        swap_file = fopen(swapfile, "wb");  // Open the swap file in write mode
        for (int i = 0; i < 1024; i++) {
            Page page;
            for (int j = 0; j < PAGE_SIZE; j++) {
                page.data[j] = 0;
            }
            fwrite(&page, sizeof(Page), 1, swap_file);
        }
    }

    fclose(swap_file);  // Close the swap file
}

// Write the physical memory to the backing store
void write_pm_to_swap(PM *pm) {
    FILE *swap_file = fopen(swapfile, "rb+");  // Open the swap file in read/write mode
    if (swap_file == NULL) {
        printf("Error: Swap file does not exist\n");
        exit(1);
    }

    for (int i = 0; i < pm->size; i++) {
        fwrite(&pm->frames[i], sizeof(Frame), 1, swap_file);
    }

    fclose(swap_file);  // Close the swap file
}

// update the LRU order
void update_lru_order(int vpn, int pm_size, int levels) {
    // if vpn is already in the order, change its position to the first
    int index = -1;
    for (int i = 0; i < pm_size; i++) {
        if (lru_order[i] == vpn) {
            index = i;
            break;
        }
    }
    if (index != -1) {
        for (int i = index; i > 0; i--) {
            lru_order[i] = lru_order[i - 1];
        }
        lru_order[0] = vpn;
    } else {
        // if vpn is not in the order, add it to the first position
        for (int i = pm_size - 1; i > 0; i--) {
            lru_order[i] = lru_order[i - 1];
        }
        lru_order[0] = vpn;
    }
    // print the LRU order
    printf("lru_order after simulation:\n");
    for (int i = 0; i < pm_size; i++) {
        printf("lru_order[%d]: %d\n", i, lru_order[i]);
    }
}

