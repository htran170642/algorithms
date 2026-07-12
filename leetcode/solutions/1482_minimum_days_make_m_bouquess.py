class Solution:
    def minDays(self, bloomDay: List[int], m: int, k: int) -> int:
        if m * k > len(bloomDay):
            return -1

        def check(day):
            bouquests = 0
            flowers = 0

            for bloom in bloomDay:
                if bloom <= day:
                    flowers += 1

                    if flowers == k:
                        bouquests += 1
                        flowers = 0

                else:
                    flowers = 0

            return bouquests >= m
        
        left = min(bloomDay)
        right = max(bloomDay)

        while left < right:

            mid = left + (right-left)//2

            if check(mid):
                right = mid
            else:
                left = mid + 1

        return left