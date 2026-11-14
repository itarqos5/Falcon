/** The player's head (the skin's face, pixel-sharp), or their initials when there is no skin picture. */
export function Avatar({ name, avatar, size }: { name: string; avatar?: string; size: number }) {
  if (avatar) {
    return <img className="avatar-img" src={avatar} width={size} height={size} alt="" draggable={false} style={{ borderRadius: Math.round(size * 0.18) }} />;
  }
  return (
    <span className="avatar-fallback" style={{ width: size, height: size, fontSize: Math.round(size * 0.38), borderRadius: Math.round(size * 0.18) }} aria-hidden="true">
      {name.slice(0, 2).toUpperCase()}
    </span>
  );
}
