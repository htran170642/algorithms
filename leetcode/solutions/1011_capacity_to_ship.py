class Solution:
    def shipWithinDays(self, weights: List[int], days: int) -> int:
        # min cap: min(weights), max_cap: sum(weights)

        def check(capacity):
            used = 1
            '''
            [1,2,3], days = 10
            used mãi = 0
            '''
            current = 0

            for w in weights:
                if current + w > capacity:
                    used += 1
                    current = 0

                current += w

            return used <= days

        l = max(weights)
        r = sum(weights)

        while l < r:
            mid = (r + l) // 2

            if check(mid):
                r = mid
            else:
                l = mid + 1

        return l