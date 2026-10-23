class Solution:
    def canJump(self, nums: list[int]) -> bool:
        n = len(nums)
        farthest = 0

        for i in range(n):
            if i > farthest:
                return False

            farthest = max(farthest, i + nums[i])

            if farthest >= n -1:
                return True

        return True