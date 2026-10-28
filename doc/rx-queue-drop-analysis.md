# Receive-queue overflow: root cause and fix

Symptom:

```
[VC] connReceiveQueue[13] full, dropping messageId=7948 ... 8027
```

followed by a multi-second traffic interruption (default reorder timeout is
8000 ms on client and server).

## Why the drop happens

`TcpVCIoThread::readFromConnection()` drains a socket as fast as the kernel
will hand it bytes and calls `dataCallback()` for every parsed DATA packet.
`TcpVirtualChannel::processReceivedData()` puts each packet on a fixed-size
per-connection SPSC queue (`CONN_RECEIVE_QUEUE_CAPACITY = 4096`). If the single
reorder thread is momentarily slower than the IO thread (scheduler hiccup, a
slow `receiveCallback`, a burst arriving on one connection), `try_enqueue()`
fails and the packet is **discarded**.

That is the wrong response on a reliable, ordered transport:

1. TCP already provides flow control. The receiver is supposed to stop reading
   when it cannot consume, which closes the TCP window and makes the sender
   back off. Instead the IO thread keeps reading and converts backpressure into
   application-level packet loss.
2. A dropped `messageId` leaves a permanent hole in the sequence. The reorder
   thread cannot deliver any later packet until the hole is filled or skipped.
3. Recovery depends on `MISSING_NOTIFY` -> `SentDataCache` -> resend. But the
   notify is delayed (`missingNotifyIntervalMs = 200 ms`) while the sender
   keeps transmitting. `SentDataCache` only retains 128 ids per shard
   (32 shards = 4096 ids, ~46 ms at 1 Gbps). By notify time the id is usually
   evicted, the resend cannot be served, and the receiver waits the full
   reorder timeout (8 s) before skipping the hole and delivering a huge burst.

So a transient receive backlog turns into seconds of head-of-line blocking for
every flow on the channel.

## Fix

- Treat a full receive queue as **backpressure**, not loss. The IO thread asks
  the channel whether the destination queue has room; if not it leaves the
  packet in its read buffer (bytes are not consumed) and removes that socket
  from the poll set. No packet is discarded, so no sequence hole is created.
- The sender eventually sees a zero TCP window. Its own `sendQueue` then fills
  and `send()` drops *new* UDP packets at ingress, **before** a message id is
  assigned. Dropping there is correct for a UDP tunnel: the packet was never
  part of the ordered stream.
- Resume reading as soon as the reorder thread frees space. A brief 5 ms poll
  timeout is used while any connection is paused, and a connection that still
  has buffered bytes is drained immediately on resume.
- Keep the old `try_enqueue()` failure check as a defensive last resort, but
  rate-limit the warning (it used to log one flushed line per dropped packet,
  which also slowed the producer down and amplified the drop burst).

This removes drops caused by queue pressure. Real loss (dead TCP connection)
still uses the existing missing-notify/resend/reorder-timeout path.
