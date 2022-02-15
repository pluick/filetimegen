Small utility to manage file rotations.

There are two usage modes. One will generate a name for a file based on the current time. The second
will take a list of names and output what should be pruned. You can specify how many to keep per
minute, hour, day, etc.

#### Example usage
Creates and keeps 8 hourly backups and 7 daily btrfs snapshots.

	btrfs subvolume snapshot -r /home /mnt/snapshots/$(filetimegen "home-{now}")
	find /mnt/snapshots -mindepth 1 -maxdepth 1 -name "home-*" -type d -printf "%f\0" \
		| filetimegen --prune home-{now} -H 8 -w 7 \
		| xargs -r -0 -I "{}" btrfs subvolume delete "/mnt/snapshots/{}"

#### Bugs
- Daylight savings time isn't handled at all. So in that 1 hour where we time travel backwards, it will output to prune a more recent file.
