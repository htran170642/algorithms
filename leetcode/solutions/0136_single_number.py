class Solution:
    def singleNumber(self, nums: List[int]) -> int:
        print(5^5^1)
        ans = nums[0]
        
        for i in range(1, len(nums)):
            ans ^= nums[i]

        return ans