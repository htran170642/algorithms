class Solution:
    def jump(self, nums: list[int]) -> int:
        farthest = 0
        cnt = 0
        current_end = 0

        for i in range(len(nums) - 1):
            farthest = max(farthest, i + nums[i])

            if i == current_end:
                cnt += 1
                current_end = farthest

            print(i)
        return cnt
