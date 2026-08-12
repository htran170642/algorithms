class Solution:
    def findKthLargest(self, nums: List[int], k: int) -> int:
        heap = []

        for val in nums:
            heapq.heappush(heap, val) # O(log k), space: O(k)

            # remove the smallest element
            if len(heap) > k:
                heapq.heappop(heap)

        print(heap)

        return heap[0]