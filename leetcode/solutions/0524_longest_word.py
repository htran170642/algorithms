class Solution:
    def findLongestWord(self, s: str, dictionary: List[str]) -> str:
        ans = ""

        def is_subsequence(word):
            i, j = 0, 0

            while i < len(s) and j < len(word):
                if s[i] == word[j]:
                    j += 1
                
                i+=1

            return j == len(word)

        for word in dictionary:
            if not is_subsequence(word):
                continue
            
            if len(word) > len(ans):
                ans = word
            elif len(word) == len(ans) and word < ans:
                ans = word

        return ans