class Solution:
    def containsNearbyDuplicate(self, nums: List[int], k: int) -> bool:
        m = {}
        for i in range(len(nums)):
            v = nums[i]
            if v in m:
                
                if abs(i - m[v]) <= k:
                    return True

            m[v] = i

        return False