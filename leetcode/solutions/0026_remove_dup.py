class Solution:
    def removeDuplicates(self, nums: List[int]) -> int:
        # s = set()
        
        # for num in nums:
        #     s.add(num)

        # print(s)
        # return list(s)

        k = 1

        for i in range(1, len(nums)):
            if nums[k-1] != nums[i]:
                nums[k] = nums[i]
                k += 1
            # 0,0,1,1,1,2,2,3,3,4
            # 0,1,1,1,1,2,2,3,3,4
            # k = 2, 0,1,2,1,1,2,2,3,3,4
            # k = 3, 0,1,2,3,1,2,2,3,3,4
            # k = 4, 0,1,2,3,4,2,2,3,3,4
            
        print(nums)
        return k

            