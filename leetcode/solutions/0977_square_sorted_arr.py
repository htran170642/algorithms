class Solution:
    def sortedSquares(self, nums: List[int]) -> List[int]:
        n = len(nums)
        result = [0] * n
        l, r = 0, n - 1

        for pos in range(n - 1, -1, -1):
            if abs(nums[l]) < abs(nums[r]):
                result[pos] = abs(nums[r])**2
                r -= 1
            else:
                result[pos] = abs(nums[l])**2
                l +=1

        return result