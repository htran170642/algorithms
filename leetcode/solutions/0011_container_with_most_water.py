from typing import List

class Solution:
    def maxArea(self, height: List[int]) -> int:
        l, r = 0, len(height) - 1
        ans = 0
        while l < r:
            h = min(height[l], height[r])
            w = r - l

            ans = max(ans, h*w)

            if height[l] > height[r]:
                r -= 1
            else:
                l += 1

        return ans

def main():
    solution = Solution()
    height = [1,8,6,2,5,4,8,3,7]

    print(solution.maxArea(height))

main()