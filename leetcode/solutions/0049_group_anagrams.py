from collections import defaultdict
from typing import List

class Solution:
    def groupAnagrams(self, strs: List[str]) -> List[List[str]]:
        
        group = defaultdict(list)

        for s in strs:

            # freq = [0] * 26
            # for ch in s:
            #     freq[ord(ch) - ord('a')] += 1 #use tuple(freq) as key to group anagrams

            key = ''.join(sorted(s))
            group[key].append(s)

        print(group.values())

        return list(group.values())

def main():
    solution = Solution()
    strs = ["eat", "tea", "tan", "ate", "nat", "bat"]
    result = solution.groupAnagrams(strs)
    print(result)  # Output: [['eat', 'tea', 'ate'], ['tan', 'nat'], ['bat']]

if __name__ == "__main__":
    main()