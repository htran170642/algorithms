class Solution:
    def leastInterval(self, tasks: List[str], n: int) -> int:
        
        freq = Counter(tasks)
        heap = [-count for count in freq.values()]
        heapq.heapify(heap)

        print(heap)

        cooldown = deque()

        time = 0

        while heap or cooldown:

            time += 1
            
            if cooldown and cooldown[0][0] == time:
                print(cooldown)
                available_time, count = cooldown.popleft()
                heapq.heappush(heap, count)

            if heap:
                count = heapq.heappop(heap)

                count += 1

                if count < 0:
                    cooldown.append((time + 1 + n, count))

        return time

