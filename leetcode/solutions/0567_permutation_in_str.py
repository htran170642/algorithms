from collections import Counter

class Solution:
    def checkInclusion(self, s1: str, s2: str) -> bool:
        if len(s1) > len(s2):
            return False

        need = Counter(s1)
        window = Counter()

        left = 0

        for right in range(len(s2)):
            window[s2[right]] += 1

            if right - left + 1 > len(s1):
                window[s2[left]] -= 1
                if window[s2[left]] == 0:
                    del window[s2[left]]

                left += 1

            if window == need:
                return True

        return False

def main():
    solution = Solution()
    s1 = "ab"
    s2 = "eidbaooo"

    print(solution.checkInclusion(s1, s2))
if __name__ == "__main__":
    main()