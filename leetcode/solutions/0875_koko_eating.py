from typing import List
import math

class Solution:
    def minEatingSpeed(self, piles: List[int], h: int) -> int:
        def check(speed):
            cnt = 0

            for pile in piles:
                cnt += math.ceil(pile/speed)

            return cnt <= h

        # search space: 1...max(piles)
        l, r = 1, max(piles)
        while l < r:
            mid = (r + l) // 2

            if check(mid):
                r = mid
            else:
                l = mid + 1

        return l

def main():
    solution = Solution()
    piles = [3,6,7,11]
    h = 8
    result = solution.minEatingSpeed(piles, h)
    print(result)  # Output: 4

if __name__ == "__main__":
    main()