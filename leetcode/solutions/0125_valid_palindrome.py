class Solution:
    def isPalindrome(self, s: str) -> bool:
        l, r = 0, len(s) - 1

        while l < r:

            while not s[l].isalnum() and l < r:
                l += 1

            while not s[r].isalnum() and l < r:
                r -= 1

            if s[l].lower() != s[r].lower():
                return False

            l += 1
            r -= 1

        return True

# time: O(n), space: O(1)

def main():
    solution = Solution()
    s = "A man, a plan, a canal: Panama"
    result = solution.isPalindrome(s)
    print(result)  # Output: True

    s = "race a car"
    result = solution.isPalindrome(s)
    print(result)  # Output: False

if __name__ == "__main__":
    main()