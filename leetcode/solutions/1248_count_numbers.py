class Solution:
    def numberOfSubarrays(self, nums: list[int], k: int) -> int:

        '''
        exactly(k) = atMost(k) - atMost(k-1)
        0 → bị trừ
        1 → bị trừ
        2 → bị trừ
        3 → còn lại
        '''

        count = {0: 1}

        prefix = 0
        result = 0

        for num in nums:
            if num % 2 == 1:
                prefix += 1
            
            need = prefix - k

            if need in count:
                result += count[need]

            count[prefix] = count.get(prefix, 0) + 1

        return result