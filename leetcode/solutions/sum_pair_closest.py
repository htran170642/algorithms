# https://www.geeksforgeeks.org/dsa/2-sum-pair-sum-closest-to-target/


def closest_pair(arr, target):

    arr.sort()

    l, r = 0, len(arr) - 1

    best_pair = []
    best_diff = float("inf")
    best_distance = -1

    while l < r:
        current_sum = arr[l] + arr[r]

        diff = abs(current_sum - target)
        distance = abs(arr[l] - arr[r])


        if diff < best_diff or (diff == best_diff and distance > best_distance):
            best_diff = diff
            best_distance = distance
            best_pair = [arr[l], arr[r]]

        if current_sum < target:
            l += 1
        elif current_sum > target:
            r -= 1
        else:
            l += 1
            r -= 1
    return best_pair


if __name__ == "__main__":
    arr = [10, 30, 20, 5]
    target = 25
    print(closest_pair(arr, target))

    arr = [5, 2, 7, 1, 4]
    target = 10
    print(closest_pair(arr, target))

    arr = [10]
    target = 10
    print(closest_pair(arr, target))

