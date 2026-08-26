class Solution:
    def reverseVowels(self, s: str) -> str:
        
        # vowels = {'a', 'e', 'i', 'o', 'u'}
        # s = list(s)
        # l, r = 0, len(s)-1
        # ans = ''

        # while l < r:
        #     while l < r and s[l].lower() not in vowels:
        #         l += 1
      
        #     while r >= 0 and s[r].lower() not in vowels:
        #         r -= 1

        #     if l >= r:
        #         break

        #     s[l], s[r] = s[r], s[l]
        #     l += 1
        #     r -= 1

        # print(s)

        # return "".join(s)


        vowels = set("aeiouAEIOU")
        s = list(s)

        l, r = 0, len(s) -1

        while l < r:
            if s[l] not in vowels:
                l += 1
            elif s[r] not in vowels:
                r -= 1
            else:
                s[l], s[r] = s[r], s[l]

                l += 1
                r -= 1

        return "".join(s)