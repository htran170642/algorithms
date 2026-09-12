class Solution:
    def checkSubarraySum(self, nums: List[int], k: int) -> bool:
        '''
        (prefix[j] - prefix[i]) % k == 0
        => prefix[j] % k == prefix[i] % k

        Same remainder → difference divisible by k.
        '''

        first_seen = {0: -1}
        prefix = 0

        for i, num in enumerate(nums):
            prefix += num

            if k == 0:
                key = prefix
            else:
                key = prefix % k

            if key in first_seen:
                if i - first_seen[key] >= 2:
                    return True

            else:
                first_seen[key] = i

        return False