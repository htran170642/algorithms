class Solution:
    def findDuplicate(self, nums: list[int]) -> int:
        result = [0]*len(nums)

        for num in nums:
            result[num] += 1

            if result[num] > 1:
                return num

        print(result)