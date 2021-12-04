Challenges:
1. What work should be performed by logger thread and whaat by main thread.
2. Reasoning about concurrency despite linearizability.
3. Although operations on volatile data structure are performed under Reader-Writer locks,
  the logging for insertion and removal is performed by logger thread even while the main 
  thread waits to acquire the writer lock. This requires careful reasoning for 
  insert and erase operations where multiple logger threads might be performing changes to the 
  same
4. The frontend of the data structure can be designed in multiple ways to support different 
  mix of read write workloads.
5. Reduce contention on the free slot queue by using better concurrent data structure/data locality to core etc.
