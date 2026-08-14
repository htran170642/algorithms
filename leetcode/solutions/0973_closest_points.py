class Solution:
    def kClosest(self, points: List[List[int]], k: int) -> List[List[int]]:
        heap = []

        for p in points:
            x, y = p[0], p[1]

            dist = -(x*x + y*y)
            heapq.heappush(heap, (dist, (x, y)))
            if len(heap) > k:
                heapq.heappop(heap) # remove phan tu nho nhat trong heap

        print(heap)

        return [[p[0], p[1]] for dist, p in heap]
            