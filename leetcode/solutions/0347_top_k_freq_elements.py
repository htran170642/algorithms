class Solution:
    def topKFrequent(self, nums: List[int], k: int) -> List[int]:
        # python ưu tiên phần tử đầu tiên trong tuple

        freq = Counter(nums)

        heap = []

        for num, count in freq.items():
            heapq.heappush(heap, (count, num))

            if len(heap) > k:
                heapq.heappop(heap)

        return [num for count, num in heap]

