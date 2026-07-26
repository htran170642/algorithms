class Solution:
    def nextGreaterElement(self, nums1: List[int], nums2: List[int]) -> List[int]:
        stack = []

        next_greater = {}
        '''
        2 -> -1
        4 -> -1
        3 -> 4
        1 -> 3
        '''
        for num in reversed(nums2):
            
            while stack and stack[-1] <= num:
                stack.pop()

            if stack:
               next_greater[num] = stack[-1]
            else:
                next_greater[num] = -1

            stack.append(num)

        return [next_greater[num] for num in nums1] 
