// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13
#define GET_BUCKET_ID(dev, blockno) ((dev + 1) * (blockno + 1) % NBUCKET)

struct hash_bucket {
    struct buf head;
    struct spinlock lock;
};

struct {
    struct spinlock lock;
    struct buf buf[NBUF];

    // Linked list of all buffers, through prev/next.
    // Sorted by how recently the buffer was used.
    // head.next is most recent, head.prev is least.
    struct hash_bucket hash_buckets[NBUCKET];
} bcache;

void hash_buckets_init(int bucket_id) {
    initlock(&bcache.hash_buckets[bucket_id].lock, "bcache");

    struct buf *b;
    // Create linked list of buffers
    bcache.hash_buckets[bucket_id].head.prev = &bcache.hash_buckets[bucket_id].head;
    bcache.hash_buckets[bucket_id].head.next = &bcache.hash_buckets[bucket_id].head;
    for (b = bcache.buf + bucket_id; b < bcache.buf + NBUF; b += NBUCKET) {
        b->next = bcache.hash_buckets[bucket_id].head.next;
        b->prev = &bcache.hash_buckets[bucket_id].head;
        initsleeplock(&b->lock, "buffer");
        bcache.hash_buckets[bucket_id].head.next->prev = b;
        bcache.hash_buckets[bucket_id].head.next = b;
    }
}

void binit(void) {
    initlock(&bcache.lock, "bcache");

    for (int i = 0; i < NBUCKET; i++) {
        hash_buckets_init(i);
    }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno) {
    struct buf *b;

    int bucket_id = GET_BUCKET_ID(dev, blockno);

    acquire(&bcache.hash_buckets[bucket_id].lock);
    // Is the block already cached?
    for (b = bcache.hash_buckets[bucket_id].head.next; b != &bcache.hash_buckets[bucket_id].head; b = b->next) {
        if (b->dev == dev && b->blockno == blockno) {
            b->refcnt++;
            release(&bcache.hash_buckets[bucket_id].lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    // Not cached.
    // Recycle the least recently used (LRU) unused buffer.
    for (b = bcache.hash_buckets[bucket_id].head.prev; b != &bcache.hash_buckets[bucket_id].head; b = b->prev) {
        if (b->refcnt == 0) {
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            b->refcnt = 1;
            release(&bcache.hash_buckets[bucket_id].lock);
            acquiresleep(&b->lock);
            return b;
        }
    }
    release(&bcache.hash_buckets[bucket_id].lock);

    // 哈希桶中没有buf，从其他哈希表中 偷 一个buf到对应的哈希桶中
    for (int steal = (bucket_id + 1) % 13; steal != bucket_id; steal = (steal + 1) % 13) {
        // 保证所有进程获取锁的顺序一致
        if (steal > bucket_id) {
            acquire(&bcache.hash_buckets[bucket_id].lock);
            acquire(&bcache.hash_buckets[steal].lock);
        } else {
            acquire(&bcache.hash_buckets[steal].lock);
            acquire(&bcache.hash_buckets[bucket_id].lock);
        }
        for (b = bcache.hash_buckets[steal].head.prev; b != &bcache.hash_buckets[steal].head; b = b->prev) {
            if (b->refcnt == 0) {
                b->dev = dev;
                b->blockno = blockno;
                b->valid = 0;
                b->refcnt = 1;
                // 将别的哈希桶里的buf放入当前bucket_id的哈希桶中
                b->next->prev = b->prev;
                b->prev->next = b->next;
                release(&bcache.hash_buckets[steal].lock);
                b->next = bcache.hash_buckets[bucket_id].head.next;
                b->prev = &bcache.hash_buckets[bucket_id].head;
                bcache.hash_buckets[bucket_id].head.next->prev = b;
                bcache.hash_buckets[bucket_id].head.next = b;
                release(&bcache.hash_buckets[bucket_id].lock);
                acquiresleep(&b->lock);
                return b;
            }
        }
        release(&bcache.hash_buckets[steal].lock);
        release(&bcache.hash_buckets[bucket_id].lock);
    }

    panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *
bread(uint dev, uint blockno) {
    struct buf *b;

    b = bget(dev, blockno);
    if (!b->valid) {
        virtio_disk_rw(b, 0);
        b->valid = 1;
    }
    return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b) {
    if (!holdingsleep(&b->lock))
        panic("bwrite");
    virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b) {
    if (!holdingsleep(&b->lock))
        panic("brelse");

    int bucket_id = GET_BUCKET_ID(b->dev, b->blockno);
    releasesleep(&b->lock);

    acquire(&bcache.hash_buckets[bucket_id].lock);
    b->refcnt--;
    if (b->refcnt == 0) {
        // no one is waiting for it.
        b->next->prev = b->prev;
        b->prev->next = b->next;
        b->next = bcache.hash_buckets[bucket_id].head.next;
        b->prev = &bcache.hash_buckets[bucket_id].head;
        bcache.hash_buckets[bucket_id].head.next->prev = b;
        bcache.hash_buckets[bucket_id].head.next = b;
    }

    release(&bcache.hash_buckets[bucket_id].lock);
}

void bpin(struct buf *b) {
    int bucket_id = GET_BUCKET_ID(b->dev, b->blockno);
    acquire(&bcache.hash_buckets[bucket_id].lock);
    b->refcnt++;
    release(&bcache.hash_buckets[bucket_id].lock);
}

void bunpin(struct buf *b) {
    int bucket_id = GET_BUCKET_ID(b->dev, b->blockno);
    acquire(&bcache.hash_buckets[bucket_id].lock);
    b->refcnt--;
    release(&bcache.hash_buckets[bucket_id].lock);
}
