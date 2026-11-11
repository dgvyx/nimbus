/* Nimbus OS - alocador de memoria simples (first-fit) */
#include "nimbus.h"

/* Heap de 8 MB em 0x200000 (apos kernel/bss, antes de wallpaper em 0x400000) */
#define HEAP_START 0x200000
#define HEAP_SIZE  (8 * 1024 * 1024)
#define HEAP_END   (HEAP_START + HEAP_SIZE)

/* Cabecalho de bloco livre/ocupado */
typedef struct Block {
    u32 size;           /* tamanho total do bloco (inclui cabecalho) */
    u32 magic;          /* 0xB10CB10C = livre, 0xDEADBEEF = ocupado */
    struct Block *next; /* proximo bloco na lista de livres */
} Block;

#define MAGIC_FREE   0xB10CB10C
#define MAGIC_USED   0xDEADBEEF
#define BLOCK_HDR_SZ sizeof(Block)

static Block *free_list = 0;
static int heap_initialized = 0;

/* Alinhamento para 8 bytes */
static u32 align8(u32 x) { return (x + 7) & ~7u; }

void mem_init(void)
{
    if (heap_initialized) return;
    Block *b = (Block *)HEAP_START;
    b->size = HEAP_SIZE;
    b->magic = MAGIC_FREE;
    b->next = 0;
    free_list = b;
    heap_initialized = 1;
}

void *kmalloc(u32 size)
{
    if (!heap_initialized) mem_init();
    if (size == 0) return 0;
    size = align8(size + BLOCK_HDR_SZ);

    Block **prev = &free_list;
    Block *curr = free_list;
    while (curr) {
        if (curr->magic != MAGIC_FREE) { /* corrupcao */
            curr = curr->next;
            continue;
        }
        if (curr->size >= size) {
            /* Divide se sobrar espaco suficiente para outro cabecalho + 8 bytes */
            if (curr->size >= size + BLOCK_HDR_SZ + 8) {
                Block *new_block = (Block *)((u8 *)curr + size);
                new_block->size = curr->size - size;
                new_block->magic = MAGIC_FREE;
                new_block->next = curr->next;
                curr->size = size;
                curr->next = new_block;
            }
            curr->magic = MAGIC_USED;
            *prev = curr->next; /* remove da lista de livres */
            return (u8 *)curr + BLOCK_HDR_SZ;
        }
        prev = &curr->next;
        curr = curr->next;
    }
    return 0; /* sem memoria */
}

void kfree(void *ptr)
{
    if (!ptr) return;
    Block *b = (Block *)((u8 *)ptr - BLOCK_HDR_SZ);
    if (b->magic != MAGIC_USED) return; /* double free ou ponteiro invalido */
    b->magic = MAGIC_FREE;
    /* Insere no inicio da lista (first-fit simples) */
    b->next = free_list;
    free_list = b;
    /* Coalescencia basica com proximo bloco se contiguo e livre */
    Block *curr = free_list;
    while (curr && curr->next) {
        if (curr->magic == MAGIC_FREE && curr->next->magic == MAGIC_FREE) {
            u8 *end = (u8 *)curr + curr->size;
            if (end == (u8 *)curr->next) {
                curr->size += curr->next->size;
                curr->next = curr->next->next;
                continue;
            }
        }
        curr = curr->next;
    }
}

void *krealloc(void *ptr, u32 new_size)
{
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return 0; }
    Block *b = (Block *)((u8 *)ptr - BLOCK_HDR_SZ);
    if (b->magic != MAGIC_USED) return 0;
    u32 old_size = b->size - BLOCK_HDR_SZ;
    if (new_size <= old_size) return ptr; /* encolher: apenas retorna mesmo ponteiro (simplificado) */
    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return 0;
    memcpy(new_ptr, ptr, old_size);
    kfree(ptr);
    return new_ptr;
}

/* Para debug: imprime estatisticas do heap */
void mem_stats(void)
{
    u32 total_free = 0, total_used = 0, blocks_free = 0, blocks_used = 0;
    Block *curr = (Block *)HEAP_START;
    u8 *end = (u8 *)HEAP_END;
    while ((u8 *)curr < end && curr->size > 0) {
        if (curr->magic == MAGIC_FREE) {
            total_free += curr->size;
            blocks_free++;
        } else if (curr->magic == MAGIC_USED) {
            total_used += curr->size;
            blocks_used++;
        } else {
            break; /* corrupcao */
        }
        curr = (Block *)((u8 *)curr + curr->size);
    }
    char buf[32];
    term_puts("Heap: ");
    itoa_(total_free / 1024, buf); term_puts(buf); term_puts(" KB livres, ");
    itoa_(total_used / 1024, buf); term_puts(buf); term_puts(" KB usados\n");
    itoa_(blocks_free, buf); term_puts(buf); term_puts(" blocos livres, ");
    itoa_(blocks_used, buf); term_puts(buf); term_puts(" blocos usados\n");
}

u32 strlen(const char *s)
{
    u32 len = 0;
    while (s[len]) len++;
    return len;
}

