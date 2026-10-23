class Solution:
    def rotate(self, nums: list[int], k: int) -> None:
        """
        Do not return anything, modify nums in-place instead.
        """
        n = len(nums)
        k %= n

        res = [0] * n

        for i in range(n):
            res[(i+k)%n] = nums[i]

        nums[:] = res